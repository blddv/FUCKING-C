#!/usr/bin/env node
import { readFileSync } from 'node:fs';
import { pathToFileURL } from 'node:url';
import { compile, VirtualMachine, encodeText, VERSION, FuckingCError } from './interpreter.mjs';

const HELP = `Fucking C ${VERSION} — все команды являются вариациями C.

Запуск:
  node cli.mjs run программа.fc [--input "текст"] [--stats]
  node cli.mjs check программа.fc
  node cli.mjs encode "текст" > программа.fc

Можно опустить run: node cli.mjs программа.fc

Команды:
  C  вправо       c  влево       CC  +1        cc  −1
  C̶  вывод        c̶  ввод        Ć   цикл      ć   конец цикла

Команды обязательно разделяются пробельными символами.

Параметры run:
  --input TEXT         Ввод в UTF-8; иначе читается перенаправленный stdin.
  --input-file PATH    Байтовый ввод из файла (несовместим с --input).
  --max-steps N        Лимит шагов, по умолчанию 1000000.
  --cells N            Размер кольцевой памяти, по умолчанию 30000.
  --max-output N       Лимит байтов вывода, по умолчанию 1048576.
  --stats              Статистика в stderr; stdout содержит только байты программы.
  --help               Эта справка.
  --version            Версия языка.

EOF записывает 0. В интерактивном терминале без --input ввод пустой.
`;

class UsageError extends Error {}

function parseRunArguments(args) {
  const result = { path: null, options: {}, input: null, inputFile: null, stats: false };
  const flags = new Set();
  const numeric = new Map([
    ['--max-steps', 'maxSteps'], ['--cells', 'tapeSize'], ['--max-output', 'maxOutputBytes'],
  ]);
  for (let i = 0; i < args.length; i += 1) {
    const arg = args[i];
    if (arg === '--stats') {
      result.stats = true;
    } else if (arg === '--input' || arg === '--input-file' || numeric.has(arg)) {
      if (flags.has(arg)) throw new UsageError(`Параметр ${arg} указан повторно.`);
      flags.add(arg);
      if (i + 1 >= args.length) throw new UsageError(`После ${arg} требуется значение.`);
      const value = args[++i];
      if (arg === '--input') result.input = value;
      else if (arg === '--input-file') result.inputFile = value;
      else {
        if (!/^\d+$/.test(value) || !Number.isSafeInteger(Number(value)) || Number(value) < 1) {
          throw new UsageError(`После ${arg} требуется положительное целое число.`);
        }
        result.options[numeric.get(arg)] = Number(value);
      }
    } else if (arg.startsWith('-')) {
      throw new UsageError(`Неизвестный параметр: ${arg}.`);
    } else if (result.path === null) {
      result.path = arg;
    } else {
      throw new UsageError(`Лишний аргумент: ${arg}.`);
    }
  }
  if (result.path === null) throw new UsageError('Укажи файл программы .fc.');
  if (result.input !== null && result.inputFile !== null) {
    throw new UsageError('Выбери только один источник: --input или --input-file.');
  }
  return result;
}

export function main(args = process.argv.slice(2)) {
  try {
    if (args.length === 0 || (args.length === 1 && args[0] === '--help')) {
      process.stdout.write(HELP);
      return 0;
    }
    if (args.length === 1 && args[0] === '--version') {
      process.stdout.write(VERSION + '\n');
      return 0;
    }
    if (args[0] === 'encode') {
      if (args.length !== 2) throw new UsageError('Формат: node cli.mjs encode "текст"');
      process.stdout.write(encodeText(args[1]) + '\n');
      return 0;
    }
    if (args[0] === 'check') {
      if (args.length !== 2) throw new UsageError('Формат: node cli.mjs check программа.fc');
      const program = compile(readFileSync(args[1], 'utf8'));
      process.stdout.write(`OK: ${program.tokens.length} команд.\n`);
      return 0;
    }

    const parsed = parseRunArguments(args[0] === 'run' ? args.slice(1) : args);
    const program = compile(readFileSync(parsed.path, 'utf8'));
    let input = new Uint8Array();
    if (parsed.input !== null) input = new TextEncoder().encode(parsed.input);
    else if (parsed.inputFile !== null) input = readFileSync(parsed.inputFile);
    else if (!process.stdin.isTTY && program.tokens.some(({ op }) => op === 'input')) input = readFileSync(0);

    const machine = new VirtualMachine(program, { ...parsed.options, input });
    let failure = null;
    try {
      machine.run();
    } catch (error) {
      failure = error;
    }
    // Preserve partial byte output even when a resource limit stops execution.
    process.stdout.write(Buffer.from(machine.output));
    if (parsed.stats) {
      process.stderr.write(`Шагов: ${machine.steps}; указатель: ${machine.pointer}; вывод: ${machine.output.length} байт.\n`);
    }
    if (failure) throw failure;
    return 0;
  } catch (error) {
    process.stderr.write(`Fucking C: ${error.message}\n`);
    if (error instanceof UsageError || error instanceof RangeError || (error instanceof FuckingCError && error.code === 'SYNTAX')) return 2;
    return 1;
  }
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = main();
}
