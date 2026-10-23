const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const header = fs.readFileSync(path.join(root, 'src/firmware_hash_script.h'), 'utf8');
const script = header.split('R"JS(')[1].split(')JS"')[0];
const context = vm.createContext({ Uint8Array, Uint32Array, DataView });
vm.runInContext(script, context);
(async () => {
  // Padding boundaries, binary bytes, known vectors, and an image near slot capacity.
  const vectors = [Buffer.alloc(0), Buffer.from('abc'), Buffer.alloc(1000000, 97)];
  for (const size of [1, 55, 56, 63, 64, 65, 119, 120, 1024, 1310720]) {
    vectors.push(crypto.randomBytes(size));
  }
  const firmware = path.join(root, '.pio/build/esp32dev/firmware.bin');
  if (fs.existsSync(firmware)) vectors.push(fs.readFileSync(firmware));
  for (const data of vectors) {
    const expected = crypto.createHash('sha256').update(data).digest('hex');
    const actual = await context.firmwareSha256(new Blob([data]));
    assert.equal(actual, expected, `SHA256 differs for ${data.length} bytes`);
  }
  console.log(`PASS: ${vectors.length} SHA256 vectors match Node crypto`);
})().catch(error => { console.error(error); process.exitCode = 1; });
