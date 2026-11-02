'use strict';
const fs = require('fs');
const s = fs.readFileSync(require('path').join(__dirname, '../dist/index.html'), 'utf8');
const report = {
  inventsAqa: /r\?"aqa":null/.test(s) || s.includes("localDev ? 'aqa'"),
  inventsAdmin: /r\?"admin":null/.test(s) || s.includes("localDev ? 'admin'"),
  admin1234: s.includes('admin1234'),
  queryUserOnly: /get\("user"\)(?!\?\?)/.test(s) || s.includes("query.get('user')"),
};
console.log(report);
if (report.inventsAqa || report.inventsAdmin || report.admin1234) process.exit(1);
