import { encodeText } from './interpreter.mjs';

const symbols = { '>': 'C', '<': 'c', '+': 'CC', '-': 'cc', '.': 'C̶', ',': 'c̶', '[': 'Ć', ']': 'ć' };

function fromBrainfuck(source) {
  const tokens = [...source].map((symbol) => symbols[symbol]);
  const lines = [];
  for (let i = 0; i < tokens.length; i += 14) lines.push(tokens.slice(i, i + 14).join(' '));
  return lines.join('\n');
}

export const PRESETS = Object.freeze({
  hello: Object.freeze({
    filename: 'hello.fc',
    source: fromBrainfuck('++++++++++[>+++++++>++++++++++>+++>+<<<<-]>++.>+.+++++++..+++.>++++++++++++++.------------.<<-----.>>+.>.'),
    input: '',
    expected: 'Hello, C!\n',
  }),
  echo: Object.freeze({
    filename: 'echo.fc',
    source: 'c̶ Ć C̶ c̶ ć',
    input: 'Привет, C!',
    expected: 'Привет, C!',
  }),
  digits: Object.freeze({
    filename: 'digits.fc',
    source: fromBrainfuck('++++++++++>++++++[>++++++++<-]<[>>.+<<-]>>>++++++++++.'),
    input: '',
    expected: '0123456789\n',
  }),
  russian: Object.freeze({
    filename: 'russian.fc',
    source: encodeText('Привет, C!\n'),
    input: '',
    expected: 'Привет, C!\n',
  }),
  empty: Object.freeze({ filename: 'program.fc', source: '', input: '', expected: '' }),
});
