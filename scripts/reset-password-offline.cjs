'use strict';

const fs = require('node:fs');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const {isDeepStrictEqual} = require('node:util');

function readRequests(file) {
  let input;
  try {
    input = JSON.parse(fs.readFileSync(file === '-' ? 0 : file, 'utf8'));
  } catch {
    throw new Error('Cannot read password reset JSON');
  }
  const requests = Array.isArray(input) ? input : [input];
  const valid = requests.every(request =>
    request && typeof request.username === 'string' && request.username.length > 0 &&
    (request.userId === undefined || (typeof request.userId === 'string' && request.userId.length > 0)) &&
    typeof request.password === 'string' && Buffer.byteLength(request.password) > 0 &&
    Buffer.byteLength(request.password) <= 72);
  assert(valid && requests.length && new Set(requests.map(request => request.username)).size === requests.length,
    'Expected distinct reset requests with valid account names and passwords');
  return requests;
}

function resetPasswords(input, output, requests) {
  let original;
  try {
    original = JSON.parse(fs.readFileSync(input));
  } catch {
    throw new Error('Cannot read stopped save JSON');
  }
  const db = structuredClone(original);
  const results = requests.map(request => {
    const entries = Object.entries(db.users || {}).filter(([, user]) => user.username === request.username);
    assert.equal(entries.length, 1, 'Exact account not found');
    const [key, user] = entries[0];
    assert(typeof user.id === 'string' && user.id.length > 0 &&
      (request.userId === undefined || user.id === request.userId), 'Account identity changed');
    user.salt = crypto.randomBytes(16).toString('hex');
    user.password = crypto.scryptSync(request.password, user.salt, 64).toString('hex');
    user.isPlainText = false;
    user.passwordChosen = true;
    delete user.passwordHash;
    return {key, username: user.username, userId: user.id, revokedSessions: 0};
  });

  const byUserId = new Map(results.map(result => [result.userId, result]));
  assert.equal(byUserId.size, results.length, 'Multiple reset requests resolve to the same account');
  for (const [token, session] of Object.entries(db.sessions || {})) {
    const result = byUserId.get(session.userId);
    if (!result) continue;
    delete db.sessions[token];
    ++result.revokedSessions;
  }

  const compare = structuredClone(db);
  for (const {key} of results) compare.users[key] = original.users[key];
  if (Object.hasOwn(original, 'sessions')) compare.sessions = original.sessions;
  assert(isDeepStrictEqual(compare, original), 'Password reset would change unrelated save data');
  fs.writeFileSync(output, JSON.stringify(db), {mode: 0o600});
  const written = JSON.parse(fs.readFileSync(output));
  return results.map(({key, ...result}, index) => {
    const user = written.users[key];
    assert(crypto.scryptSync(requests[index].password, user.salt, 64).toString('hex') === user.password,
      'Written password failed verification');
    return {...result, passwordVerified: true};
  });
}

if (require.main === module) {
  try {
    const [input, output, credentials] = process.argv.slice(2);
    const results = resetPasswords(input, output, readRequests(credentials));
    console.log(JSON.stringify(results.length === 1 ? results[0] : results));
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}

module.exports = {readRequests};
