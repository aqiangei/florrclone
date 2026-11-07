'use strict';
const assert=require('node:assert/strict');
const assetNames=['bundle.js','bundle.wasm','bundle.js.bin','bundle.wasm.bin'];
const releasePattern=/^\/opt\/florrclone\/releases\/[a-zA-Z0-9_-]+$/;
// The entry must overwrite this private hop header. Trust it on the backend
// only when the TCP peer is the configured entry server.
const entryClientHeader = '        proxy_set_header X-Florr-Client-IP $remote_addr;';

function readAssetHashes(ssh,release) {
  assert.match(release,releasePattern);
  const lines=ssh('cd '+release+' && sha256sum -- '+assetNames.join(' ')).trim().split(/\r?\n/);
  assert.equal(lines.length,assetNames.length,'Incomplete client asset checksums');
  return Object.fromEntries(assetNames.map((name,i)=>{
    const row=lines[i].match(/^([a-f0-9]{64})  (.+)$/);
    assert(row&&row[2]===name,'Unexpected checksum for '+name);
    return [name,row[1]];
  }));
}

function cacheAssetPath(name,assetHashes) {
  assert(assetNames.includes(name),'Unknown client asset '+name);
  const hash=assetHashes[name];
  assert.match(hash,/^[a-f0-9]{64}$/);
  return '/client-assets/sha256-'+hash+'/'+name;
}

function configure(base,{release,assetHost,downloadQueue=false,entryProxy,assetHashes}) {
  assert.match(release,releasePattern);
  assert.match(assetHost,/^[a-zA-Z0-9.-]+$/);
  if(entryProxy){
    assert.match(entryProxy,/^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$/);
    assert.equal(require('node:net').isIP(entryProxy),4);
    const listen='    listen [::]:8088 default_server;';
    assert(base.includes(listen));
    base=base.replace(listen,listen+'\n    set_real_ip_from '+entryProxy+';\n    real_ip_header X-Florr-Client-IP;');
  }
  const version=release.split('/').pop();
  const assetBase='http://'+assetHost+':8088/client-assets/'+version+'/';
  const queueRoutes=downloadQueue?`
    location ^~ /download-queue/ {
        if (-f /var/www/florrclone-maintenance/enabled) { return 503; }
        proxy_pass http://127.0.0.1:8789;
        proxy_set_header Connection "";
        proxy_read_timeout 10s;
    }
    location = /__download_authorize {
        internal;
        proxy_pass http://127.0.0.1:8789/authorize;
        proxy_pass_request_body off;
        proxy_set_header Content-Length "";
        proxy_set_header X-Original-URI $request_uri;
        proxy_set_header X-Download-Ticket $http_x_download_ticket;
    }
`:`
    # Older client builds still ask for a download ticket. Let those clients
    # proceed immediately, while the versioned assets are served directly.
    location ^~ /download-queue/ {
        if (-f /var/www/florrclone-maintenance/enabled) { return 503; }
        default_type application/json;
        add_header Cache-Control "no-store" always;
        return 200 '{"ok":true,"ticket":"direct-download","state":"ready","ahead":0,"active":0,"assetBase":"${assetBase}"}';
    }
`;
  const accessHeaders=`
        add_header Access-Control-Allow-Origin "*" always;
        add_header Access-Control-Allow-Headers "X-Download-Ticket, Range" always;
        add_header Access-Control-Allow-Methods "GET, OPTIONS" always;
        add_header Access-Control-Expose-Headers "Content-Range, Content-Encoding, ETag" always;
        add_header Cross-Origin-Resource-Policy "cross-origin" always;`;
  const assetFile=(location,file)=>`
    location ${location} {
        ${downloadQueue?'auth_request /__download_authorize;':''}
        if ($request_method = OPTIONS) { return 204; }
        alias ${release}/${file};
        default_type application/octet-stream;
        types { application/wasm wasm; application/javascript js; }
        gzip off;
        sendfile on;
        etag on;${accessHeaders}
        add_header Cache-Control "public, max-age=31536000, immutable";
    }
`;
  const assets=assetHashes?assetNames.map(name=>{
    const stable=cacheAssetPath(name,assetHashes);
    return `
    location = /client-assets/${version}/${name} {
        if ($request_method = OPTIONS) { return 204; }
        absolute_redirect off;${accessHeaders}
        add_header Cache-Control "no-store" always;
        return 302 ${stable}$is_args$args;
    }
`+assetFile('= '+stable,name);
  }).join(''):assetFile('~ ^/client-assets/'+version+'/(bundle\\.(?:js|wasm)(?:\\.bin)?)$','$1');
  const routes=queueRoutes+`
    location = / {
        if (-f /var/www/florrclone-maintenance/enabled) { return 503; }
        root ${release};
        index index.html;
        default_type text/html;
        add_header Cache-Control "no-store" always;
    }
    location = /index.html {
        if (-f /var/www/florrclone-maintenance/enabled) { return 503; }
        root ${release};
        default_type text/html;
        add_header Cache-Control "no-store" always;
    }

    # The release pins the client files. Identical bytes share a cache address.
    ${assets}
    location ~ ^/(bundle\\.(?:js|wasm)(?:\\.bin)?)$ {
        if (-f /var/www/florrclone-maintenance/enabled) { return 503; }
        add_header Cache-Control "no-store" always;
        add_header Access-Control-Allow-Origin "*" always;
        add_header Access-Control-Allow-Headers "X-Download-Ticket" always;
        add_header Access-Control-Allow-Methods "GET, OPTIONS" always;
        add_header Cross-Origin-Resource-Policy "cross-origin" always;
        return 302 http://${assetHost}:8088/client-assets/${version}/$1;
    }
`;
  assert(base.includes('    location / {'));
  return base.replace('    location / {',routes+'    location / {');
}
function downloadQueueUnit(release,assetHost,slots=8) {
  assert.match(release,/^\/opt\/florrclone\/releases\/[a-zA-Z0-9_-]+$/);
  assert.match(assetHost,/^[a-zA-Z0-9.-]+$/);
  assert(Number.isInteger(slots)&&slots>=1&&slots<=32);
  const assetBase='http://'+assetHost+':8088/client-assets/'+release.split('/').pop()+'/';
  return `[Unit]
Description=Florr client download queue
After=network.target
[Service]
User=florrclone
Environment=PORT=8789
Environment=DOWNLOAD_SLOTS=${slots}
Environment=DOWNLOAD_ASSET_BASE=${assetBase}
ExecStart=/opt/florrclone/runtime/bin/node ${release}/download-queue.cjs
Restart=on-failure
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
[Install]
WantedBy=multi-user.target
`;
}
module.exports={configure,entryClientHeader,downloadQueueUnit,readAssetHashes,cacheAssetPath,assetNames};
