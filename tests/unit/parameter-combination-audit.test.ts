import { describe, expect, it } from 'vitest';
import { stripShebang } from '../parameter-combination-audit-helpers.mjs';

describe('parameter combination audit source preparation', () => {
  it.each([
    ['LF', '#!/usr/bin/env node\nconst value = 1;', 'const value = 1;'],
    ['CRLF', '#!/usr/bin/env node\r\nconst value = 1;', 'const value = 1;'],
    ['CR', '#!/usr/bin/env node\rconst value = 1;', 'const value = 1;'],
    ['BOM + CRLF', '\uFEFF#!/usr/bin/env node\r\nconst value = 1;', 'const value = 1;']
  ])('strips a %s shebang line before evaluation', (_name, source, expected) => {
    expect(stripShebang(source)).toBe(expected);
  });

  it('does not remove a shebang-looking line after the first line', () => {
    const source = 'const first = true;\n#!/usr/bin/env node\nconst second = true;';
    expect(stripShebang(source)).toBe(source);
  });
});
