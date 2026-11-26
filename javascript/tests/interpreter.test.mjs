import test from 'node:test';
import assert from 'node:assert/strict';
import { COMMANDS, compile, normalizeToken, VirtualMachine, run, encodeText, FuckingCError, MAX_SOURCE_LENGTH } from '../interpreter.mjs';
import { PRESETS } from '../examples.mjs';

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const symbols = { '>': 'C', '<': 'c', '+': 'CC', '-': 'cc', '.': 'C̶', ',': 'c̶', '[': 'Ć', ']': 'ć' };
const fc = (source) => [...source].map((char) => symbols[char]).join(' ');
const text = (machine) => decoder.decode(machine.getOutputBytes());

test('there are exactly eight distinct C commands', () => {
  assert.equal(COMMANDS.length, 8);
  assert.equal(new Set(COMMANDS.map(({ token }) => token)).size, 8);
  assert.deepEqual(compile('C c CC cc C̶ c̶ Ć ć').tokens.map(({ token }) => token), COMMANDS.map(({ token }) => token));
});

test('CC is one increment, C C is two pointer movements', () => {
  assert.equal(run('CC').memory[0], 1);
  assert.equal(run('C C').pointer, 2);
  assert.throws(() => compile('CCCC'), /Разделяй команды пробелами/);
  assert.throws(() => compile('Cc'), FuckingCError);
});

test('whitespace, tabs, CRLF and a UTF-8 BOM are allowed', () => {
  const program = compile('﻿CC\tC\r\n  cc\rC̶\n');
  assert.equal(program.tokens.length, 4);
  assert.deepEqual(program.tokens.map(({ line, column }) => [line, column]), [[1, 2], [1, 5], [2, 3], [3, 1]]);
});

test('Unicode line separators have correct locations', () => {
  const tokens = compile('C c CC').tokens;
  assert.deepEqual(tokens.map(({ line, column }) => [line, column]), [[1, 1], [2, 1], [3, 1]]);
});

test('Cyrillic lookalikes and decomposed acute accents normalize', () => {
  assert.equal(normalizeToken('С̶'), 'C̶');
  assert.equal(normalizeToken('с̶'), 'c̶');
  assert.deepEqual(compile('С с СС сс С̶ с̶ С́ с́').tokens.map(({ token }) => token), COMMANDS.map(({ token }) => token));
  assert.deepEqual(compile('Ć ć').tokens.map(({ token }) => token), ['Ć', 'ć']);
});

test('unknown symbols and comments are rejected with source locations', () => {
  assert.throws(() => compile('C\n  CCCC'), (error) => error.code === 'SYNTAX' && error.line === 2 && error.column === 3 && error.start === 4 && error.end === 8);
  for (const source of ['+', '# comment', 'С++', 'Ç', 'C̶C', '©']) assert.throws(() => compile(source), FuckingCError);
});

test('loop matching is checked before execution', () => {
  assert.throws(() => compile('C ć'), /без открывающей/);
  assert.throws(() => compile('Ć CC'), /не найдена/);
  assert.throws(() => compile('Ć Ć ć'), (error) => error.line === 1 && error.column === 1);
  assert.deepEqual(compile('Ć Ć ć ć').jumps, [3, 2, 1, 0]);
});

test('compiled programs and their instructions are immutable', () => {
  const program = compile('CC');
  assert.ok(Object.isFrozen(program));
  assert.ok(Object.isFrozen(program.tokens));
  assert.ok(Object.isFrozen(program.tokens[0]));
  assert.ok(Object.isFrozen(program.jumps));
});

test('empty programs halt with zero steps', () => {
  const machine = run(' \n\t');
  assert.equal(machine.halted, true);
  assert.equal(machine.steps, 0);
  assert.equal(machine.step(), false);
  assert.equal(machine.memory.length, 30000);
});

test('byte arithmetic wraps at 0 and 255', () => {
  assert.deepEqual([...run('cc C̶ CC C̶').getOutputBytes()], [255, 0]);
  assert.deepEqual([...run(Array(256).fill('CC').join(' ') + ' C̶').getOutputBytes()], [0]);
});

test('pointer wraps at both ends of the circular tape', () => {
  const machine = run('c CC C̶ C C̶', { tapeSize: 3 });
  assert.deepEqual([...machine.getOutputBytes()], [1, 0]);
  assert.deepEqual([...machine.memory], [0, 0, 1]);
  assert.equal(machine.pointer, 0);
  assert.equal(run('C C C CC', { tapeSize: 3 }).memory[0], 1);
});

test('input and output operate on exact bytes; EOF writes zero', () => {
  const machine = run('c̶ C̶ c̶ C̶ c̶ C̶', { input: new Uint8Array([0, 255]) });
  assert.deepEqual([...machine.getOutputBytes()], [0, 255, 0]);
  assert.equal(machine.inputOffset, 2);
});

test('the VM copies input so the caller cannot change it during execution', () => {
  const input = new Uint8Array([65]);
  const machine = new VirtualMachine(compile('c̶ C̶'), { input });
  input[0] = 90;
  assert.equal(text(machine.run()), 'A');
});

test('UTF-8 text survives byte input and output', () => {
  const input = 'Привет, C! 🦊\n';
  assert.equal(text(run(PRESETS.echo.source, { input: encoder.encode(input) })), input);
});

test('echo stops at NUL as documented', () => {
  assert.equal(text(run(PRESETS.echo.source, { input: new Uint8Array([65, 0, 66]) })), 'A');
});

test('nested loops work and zero-valued loops are skipped', () => {
  assert.deepEqual([...run(fc('++[>++[>+++<-]<-]>>.')).getOutputBytes()], [12]);
  const skipped = run(fc('[+[+].].'));
  assert.deepEqual([...skipped.getOutputBytes()], [0]);
  assert.equal(skipped.steps, 2);
});

test('the step API exposes state without executing ahead', () => {
  const machine = new VirtualMachine(compile('CC C CC C̶'));
  assert.equal(machine.currentToken.token, 'CC');
  machine.step();
  assert.equal(machine.memory[0], 1);
  assert.equal(machine.steps, 1);
  assert.equal(machine.pointer, 0);
  assert.equal(machine.currentToken.token, 'C');
  machine.step();
  assert.equal(machine.pointer, 1);
  machine.run();
  assert.equal(machine.halted, true);
  assert.equal(machine.currentToken, null);
  assert.deepEqual([...machine.getOutputBytes()], [1]);
});

test('an infinite loop stops at the exact step limit', () => {
  const machine = new VirtualMachine(compile('CC Ć ć'), { maxSteps: 10 });
  let failure;
  assert.throws(() => machine.run(), (error) => { failure = error; return error.code === 'STEP_LIMIT' && error.start !== null; });
  assert.equal(machine.steps, 10);
  assert.equal(machine.halted, false);
  assert.throws(() => machine.step(), (error) => error === failure);
});

test('a program may finish exactly at the step limit', () => {
  const machine = run('CC C̶', { maxSteps: 2 });
  assert.equal(machine.halted, true);
  assert.equal(machine.steps, 2);
});

test('output has a separate byte limit, preserving earlier output', () => {
  const machine = new VirtualMachine(compile('C̶ C̶ C̶'), { maxOutputBytes: 2 });
  assert.throws(() => machine.run(), (error) => error.code === 'OUTPUT_LIMIT');
  assert.deepEqual(machine.output, [0, 0]);
  assert.equal(machine.steps, 2);
});

test('invalid limits, memory sizes and input types are rejected', () => {
  const program = compile('');
  for (const key of ['tapeSize', 'maxSteps', 'maxOutputBytes']) {
    for (const value of [0, -1, 1.5, Infinity, NaN, '10', Number.MAX_SAFE_INTEGER + 1]) {
      assert.throws(() => new VirtualMachine(program, { [key]: value }), RangeError);
    }
  }
  assert.throws(() => new VirtualMachine(program, { tapeSize: 1000001 }), RangeError);
  assert.throws(() => new VirtualMachine(program, { input: 'text' }), TypeError);
  assert.throws(() => new VirtualMachine({}), TypeError);
  assert.throws(() => compile(7), TypeError);
});

test('large source is rejected before tokenization', () => {
  assert.throws(() => compile(' '.repeat(MAX_SOURCE_LENGTH + 1)), (error) => error.code === 'SOURCE_LIMIT');
});

test('text generator handles ASCII, Unicode, emoji, zero bytes and empty input', () => {
  for (const input of ['', 'C', 'Hello, C!\n', 'Привет, мир!', '🦊\n汉字', '\u0000\u007f\u0080ÿ', '\ud800']) {
    const source = encodeText(input);
    assert.deepEqual(run(source).getOutputBytes(), encoder.encode(input));
    if (source) assert.ok(source.split(/\s+/u).every((token) => ['CC', 'cc', 'C̶'].includes(token)));
  }
});

test('generator enforces a 1024-byte UTF-8 limit', () => {
  assert.throws(() => encodeText('a'.repeat(1025)), RangeError);
  assert.throws(() => encodeText('я'.repeat(513)), RangeError);
  assert.equal(text(run(encodeText('a'.repeat(1024)))), 'a'.repeat(1024));
  const worstCase = encodeText('\u0080'.repeat(512));
  assert.ok(worstCase.length < MAX_SOURCE_LENGTH);
  assert.equal(text(run(worstCase)), '\u0080'.repeat(512));
  assert.throws(() => encodeText(42), TypeError);
});

for (const [name, preset] of Object.entries(PRESETS)) {
  test(`example ${name} produces its expected output`, () => {
    assert.equal(text(run(preset.source, { input: encoder.encode(preset.input) })), preset.expected);
  });
}

function reference(source, input, limit) {
  const tape = new Uint8Array(7);
  const output = [];
  const jumps = [];
  const stack = [];
  for (let i = 0; i < source.length; i += 1) {
    if (source[i] === '[') stack.push(i);
    if (source[i] === ']') { const open = stack.pop(); jumps[open] = i; jumps[i] = open; }
  }
  let pc = 0;
  let pointer = 0;
  let steps = 0;
  let read = 0;
  while (pc < source.length && steps < limit) {
    let next = pc + 1;
    switch (source[pc]) {
      case '>': pointer = (pointer + 1) % 7; break;
      case '<': pointer = (pointer + 6) % 7; break;
      case '+': tape[pointer] = (tape[pointer] + 1) % 256; break;
      case '-': tape[pointer] = (tape[pointer] + 255) % 256; break;
      case '.': output.push(tape[pointer]); break;
      case ',': tape[pointer] = input[read++] ?? 0; break;
      case '[': if (!tape[pointer]) next = jumps[pc] + 1; break;
      case ']': if (tape[pointer]) next = jumps[pc] + 1; break;
    }
    steps += 1;
    pc = next;
  }
  return { tape, output, pointer, steps, halted: pc >= source.length };
}

test('200 deterministic random programs agree with an independent reference VM', () => {
  let seed = 137;
  const random = (n) => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed % n; };
  for (let sample = 0; sample < 200; sample += 1) {
    let source = '';
    let depth = 0;
    for (let i = 0; i < 60; i += 1) {
      const choice = random(10);
      if (choice === 8 && depth < 3) { source += '['; depth += 1; }
      else if (choice === 9 && depth > 0) { source += ']'; depth -= 1; }
      else source += '><++--.,'[random(8)];
    }
    source += ']'.repeat(depth);
    const input = new Uint8Array([random(256), random(256), 0, 65]);
    const expected = reference(source, input, 300);
    const actual = new VirtualMachine(compile(fc(source)), { tapeSize: 7, maxSteps: 300, input });
    try { actual.run(); } catch (error) { assert.equal(error.code, 'STEP_LIMIT'); }
    assert.deepEqual(actual.memory, expected.tape, `memory: ${source}`);
    assert.deepEqual(actual.output, expected.output, `output: ${source}`);
    assert.equal(actual.pointer, expected.pointer);
    assert.equal(actual.steps, expected.steps);
    assert.equal(actual.halted, expected.halted);
  }
});
