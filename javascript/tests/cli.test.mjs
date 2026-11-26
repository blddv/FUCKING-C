import test, { after } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';
import { join } from 'node:path';
import { run } from '../interpreter.mjs';
import { PRESETS } from '../examples.mjs';

const cli = fileURLToPath(new URL('../cli.mjs', import.meta.url));
const temp = mkdtempSync(join(tmpdir(), 'fucking-c-tests-'));
const fixture = (name, source) => { const path = join(temp, name); writeFileSync(path, source); return path; };
const hello = fixture('hello.fc', PRESETS.hello.source);
const echo = fixture('echo.fc', PRESETS.echo.source);
const invoke = (args, input = '') => spawnSync(process.execPath, [cli, ...args], { input, timeout: 5000 });
after(() => rmSync(temp, { recursive: true, force: true }));

test('CLI help and version', () => {
  for (const args of [[], ['--help']]) {
    const result = invoke(args);
    assert.equal(result.status, 0);
    assert.match(result.stdout.toString(), /Fucking C/);
    assert.equal(result.stderr.length, 0);
  }
  assert.equal(invoke(['--version']).stdout.toString(), '1.0.0\n');
});

test('CLI runs a file with or without the run subcommand', () => {
  for (const args of [[hello], ['run', hello]]) {
    const result = invoke(args);
    assert.equal(result.status, 0);
    assert.equal(result.stdout.toString(), 'Hello, C!\n');
    assert.equal(result.stderr.length, 0);
  }
});

test('CLI --input and piped stdin preserve Unicode', () => {
  assert.equal(invoke([echo, '--input', 'Привет 🦊']).stdout.toString(), 'Привет 🦊');
  assert.equal(invoke([echo], 'Из stdin\n').stdout.toString(), 'Из stdin\n');
  assert.equal(invoke([echo, '--input', ''], 'ignored').stdout.length, 0);
});

test('CLI byte input file and output are not UTF-8 transcoded', () => {
  const path = fixture('bytes.bin', Buffer.from([0, 255]));
  const source = fixture('binary.fc', 'c̶ C̶ c̶ C̶');
  const result = invoke([source, '--input-file', path]);
  assert.equal(result.status, 0);
  assert.deepEqual(result.stdout, Buffer.from([0, 255]));
});

test('CLI syntax check does not execute or read stdin', () => {
  const result = invoke(['check', echo]);
  assert.equal(result.status, 0);
  assert.equal(result.stdout.toString(), 'OK: 5 команд.\n');
});

test('CLI text encoder emits only compilable source', () => {
  const result = invoke(['encode', 'Только C!']);
  assert.equal(result.status, 0);
  const machine = run(result.stdout.toString());
  assert.equal(new TextDecoder().decode(machine.getOutputBytes()), 'Только C!');
});

test('CLI statistics go to stderr rather than corrupting output', () => {
  const result = invoke([hello, '--stats']);
  assert.equal(result.status, 0);
  assert.equal(result.stdout.toString(), 'Hello, C!\n');
  assert.match(result.stderr.toString(), /Шагов: \d+; указатель: \d+; вывод: 10 байт/);
});

test('CLI rejects missing, conflicting, repeated and invalid options', () => {
  const cases = [
    ['run'], [hello, '--input'], [hello, '--cells', '0'], [hello, '--cells', '1.5'],
    [hello, '--max-steps', '-1'], [hello, '--max-output', 'NaN'], [hello, '--wat'],
    [hello, '--input', 'x', '--input-file', 'x'], [hello, '--input', 'a', '--input', 'b'],
    ['encode'], ['check'], ['check', hello, '--stats'], [hello, 'extra'], [hello, '--cells', '1000001'],
  ];
  for (const args of cases) {
    const result = invoke(args);
    assert.equal(result.status, 2, args.join(' '));
    assert.match(result.stderr.toString(), /Fucking C:/);
    assert.equal(result.stdout.length, 0);
  }
});

test('CLI syntax errors have locations and no JavaScript stack', () => {
  const path = fixture('invalid.fc', 'C\n  ć');
  const result = invoke([path]);
  assert.equal(result.status, 2);
  assert.match(result.stderr.toString(), /строка 2, столбец 3/);
  assert.doesNotMatch(result.stderr.toString(), /at main/);
});

test('CLI runtime limits preserve partial output and return a failure', () => {
  const source = fixture('loop.fc', 'CC C̶ Ć ć');
  const result = invoke([source, '--max-steps', '20', '--stats']);
  assert.equal(result.status, 1);
  assert.deepEqual(result.stdout, Buffer.from([1]));
  assert.match(result.stderr.toString(), /Шагов: 20/);
  assert.match(result.stderr.toString(), /лимит 20 шагов/);
  const outputLimited = invoke([fixture('output.fc', 'C̶ C̶ C̶'), '--max-output', '2']);
  assert.equal(outputLimited.status, 1);
  assert.deepEqual(outputLimited.stdout, Buffer.from([0, 0]));
});

test('CLI reports missing files', () => {
  const result = invoke([join(temp, 'missing.fc')]);
  assert.equal(result.status, 1);
  assert.match(result.stderr.toString(), /ENOENT/);
});
