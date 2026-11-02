'use strict';
// Keep the WASM backend private behind the existing Nginx frontend.
const net = require('node:net');
const http = require('node:http');
const listen = net.Server.prototype.listen;
net.Server.prototype.listen = function (...args) {
  if (typeof args[0] === 'number' && (args.length === 1 || typeof args[1] === 'function'))
    args.splice(1, 0, process.env.HOST || '127.0.0.1');
  return listen.apply(this, args);
};
const createServer = http.createServer;
http.createServer = function (...args) {
  const index = args.length - 1;
  const handler = args[index];
  if (typeof handler === 'function') args[index] = function (req, res) {
    if (req.url === '/api/health') {
      res.writeHead(200, {'Content-Type':'application/json','Cache-Control':'no-store'});
      res.end(JSON.stringify({ok:true,implementation:'cpp-wasm',uptimeSeconds:process.uptime()}));
      return;
    }
    return handler.call(this, req, res);
  };
  return createServer.apply(this, args);
};
