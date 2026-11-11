location = /public-skins.json {
    alias /var/lib/florr-public-skins/skins.json;
    default_type application/json;
    add_header Cache-Control "no-cache" always;
    add_header X-Content-Type-Options nosniff always;
    limit_except GET { deny all; }
}
