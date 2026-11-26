// Fucking C — a byte-oriented esolang whose commands are all variations of C.

export const VERSION = '1.0.0';
export const DEFAULT_TAPE_SIZE = 30_000;
export const DEFAULT_MAX_STEPS = 1_000_000;
export const DEFAULT_MAX_OUTPUT = 1_048_576;
export const MAX_SOURCE_LENGTH = 1_000_000;

export const COMMANDS = Object.freeze([
  { token: 'C', op: 'right', title: 'Вправо', description: 'Перейти к следующей ячейке.' },
  { token: 'c', op: 'left', title: 'Влево', description: 'Перейти к предыдущей ячейке.' },
  { token: 'CC', op: 'increment', title: 'Прибавить', description: 'Увеличить значение на 1.' },
  { token: 'cc', op: 'decrement', title: 'Вычесть', description: 'Уменьшить значение на 1.' },
  { token: 'C̶', op: 'output', title: 'Вывести', description: 'Вывести байт из текущей ячейки.' },
  { token: 'c̶', op: 'input', title: 'Прочитать', description: 'Прочитать один байт; конец ввода → 0.' },
  { token: 'Ć', op: 'open', title: 'Начать цикл', description: 'Повторять, пока ячейка не равна 0.' },
  { token: 'ć', op: 'close', title: 'Закончить цикл', description: 'Вернуться к парной Ć, если ячейка ≠ 0.' },
].map(Object.freeze));

const operations = new Map(COMMANDS.map(({ token, op }) => [token, op]));

export class FuckingCError extends Error {
  constructor(message, token = null, code = 'SYNTAX') {
    const location = token ? ` (строка ${token.line}, столбец ${token.column})` : '';
    super(message + location);
    this.name = 'FuckingCError';
    this.code = code;
    this.line = token?.line ?? null;
    this.column = token?.column ?? null;
    this.start = token?.start ?? null;
    this.end = token?.end ?? null;
  }
}

export function normalizeToken(raw) {
  // Visually identical Cyrillic С/с are accepted, including decorated forms.
  return raw.replaceAll('С', 'C').replaceAll('с', 'c').normalize('NFC');
}

export function compile(source) {
  if (typeof source !== 'string') throw new TypeError('Исходный код должен быть строкой.');
  if (source.length > MAX_SOURCE_LENGTH) {
    throw new FuckingCError('Исходный код длиннее 1 000 000 символов UTF-16.', null, 'SOURCE_LIMIT');
  }

  const tokens = [];
  const jumps = [];
  const stack = [];
  let line = 1;
  let column = 1;

  for (const match of source.matchAll(/\s+|\S+/gu)) {
    const raw = match[0];
    if (!/^\s/u.test(raw)) {
      const token = Object.freeze({
        raw,
        token: normalizeToken(raw),
        op: operations.get(normalizeToken(raw)),
        line,
        column,
        start: match.index,
        end: match.index + raw.length,
      });
      if (!token.op) {
        const preview = raw.length > 24 ? raw.slice(0, 24) + '…' : raw;
        throw new FuckingCError(`Неизвестная команда ${JSON.stringify(preview)}. Разделяй команды пробелами.`, token);
      }

      const index = tokens.length;
      tokens.push(token);
      jumps.push(-1);
      if (token.op === 'open') stack.push(index);
      if (token.op === 'close') {
        if (!stack.length) throw new FuckingCError('Закрывающая ć без открывающей Ć.', token);
        const open = stack.pop();
        jumps[open] = index;
        jumps[index] = open;
      }
    }

    const lines = raw.split(/\r\n|[\r\n\u{2028}\u{2029}]/u);
    if (lines.length > 1) {
      line += lines.length - 1;
      column = [...lines.at(-1)].length + 1;
    } else {
      column += [...raw].length;
    }
  }

  if (stack.length) {
    throw new FuckingCError('Для Ć не найдена закрывающая ć.', tokens[stack.at(-1)]);
  }
  return Object.freeze({ source, tokens: Object.freeze(tokens), jumps: Object.freeze(jumps) });
}

function positiveInteger(value, name) {
  if (!Number.isSafeInteger(value) || value < 1) {
    throw new RangeError(`${name}: требуется положительное безопасное целое число.`);
  }
}

export class VirtualMachine {
  constructor(program, {
    input = new Uint8Array(),
    tapeSize = DEFAULT_TAPE_SIZE,
    maxSteps = DEFAULT_MAX_STEPS,
    maxOutputBytes = DEFAULT_MAX_OUTPUT,
  } = {}) {
    positiveInteger(tapeSize, 'Размер памяти');
    positiveInteger(maxSteps, 'Лимит шагов');
    positiveInteger(maxOutputBytes, 'Лимит вывода');
    if (tapeSize > 1_000_000) throw new RangeError('Размер памяти не может превышать 1 000 000 ячеек.');
    if (!(input instanceof Uint8Array)) throw new TypeError('Ввод должен быть Uint8Array.');
    if (!program || !Array.isArray(program.tokens) || !Array.isArray(program.jumps)) {
      throw new TypeError('Ожидается программа, созданная compile().');
    }

    this.program = program;
    this.memory = new Uint8Array(tapeSize);
    this.input = new Uint8Array(input);
    this.inputOffset = 0;
    this.output = [];
    this.pointer = 0;
    this.pc = 0;
    this.steps = 0;
    this.maxSteps = maxSteps;
    this.maxOutputBytes = maxOutputBytes;
    this.halted = program.tokens.length === 0;
    this.error = null;
  }

  get currentToken() {
    return this.program.tokens[this.pc] ?? null;
  }

  getOutputBytes() {
    return Uint8Array.from(this.output);
  }

  step() {
    if (this.error) throw this.error;
    if (this.halted) return false;

    const instruction = this.currentToken;
    try {
      if (this.steps >= this.maxSteps) {
        throw new FuckingCError(`Достигнут лимит ${this.maxSteps.toLocaleString('ru-RU')} шагов. Возможно, цикл бесконечный.`, instruction, 'STEP_LIMIT');
      }
      let next = this.pc + 1;
      switch (instruction.op) {
        case 'right':
          this.pointer = (this.pointer + 1) % this.memory.length;
          break;
        case 'left':
          this.pointer = (this.pointer + this.memory.length - 1) % this.memory.length;
          break;
        case 'increment':
          this.memory[this.pointer] = (this.memory[this.pointer] + 1) & 255;
          break;
        case 'decrement':
          this.memory[this.pointer] = (this.memory[this.pointer] + 255) & 255;
          break;
        case 'output':
          if (this.output.length >= this.maxOutputBytes) {
            throw new FuckingCError(`Достигнут лимит вывода: ${this.maxOutputBytes} байт.`, instruction, 'OUTPUT_LIMIT');
          }
          this.output.push(this.memory[this.pointer]);
          break;
        case 'input':
          this.memory[this.pointer] = this.inputOffset < this.input.length ? this.input[this.inputOffset++] : 0;
          break;
        case 'open':
          if (this.memory[this.pointer] === 0) next = this.program.jumps[this.pc] + 1;
          break;
        case 'close':
          if (this.memory[this.pointer] !== 0) next = this.program.jumps[this.pc] + 1;
          break;
        default:
          throw new FuckingCError('Повреждённая программа: неизвестная операция.', instruction, 'INVALID_PROGRAM');
      }
      this.pc = next;
      this.steps += 1;
      this.halted = this.pc >= this.program.tokens.length;
      return true;
    } catch (error) {
      this.error = error;
      throw error;
    }
  }

  run() {
    while (!this.halted) this.step();
    return this;
  }
}

export function run(source, options) {
  return new VirtualMachine(compile(source), options).run();
}

export function encodeText(text) {
  if (typeof text !== 'string') throw new TypeError('Ожидается текстовая строка.');
  const bytes = new TextEncoder().encode(text);
  if (bytes.length > 1024) throw new RangeError('Генератор принимает до 1024 байт UTF-8.');
  const tokens = [];
  let current = 0;
  for (const byte of bytes) {
    const distance = (byte - current + 256) % 256;
    const token = distance <= 128 ? 'CC' : 'cc';
    const count = distance <= 128 ? distance : 256 - distance;
    for (let i = 0; i < count; i += 1) tokens.push(token);
    tokens.push('C̶');
    current = byte;
  }
  const lines = [];
  for (let i = 0; i < tokens.length; i += 16) lines.push(tokens.slice(i, i + 16).join(' '));
  return lines.join('\n');
}
