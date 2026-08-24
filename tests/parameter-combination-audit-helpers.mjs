/**
 * Remove a Unix-style interpreter directive before source is evaluated as a
 * function body. The line-ending class is explicit because JavaScript's dot
 * does not match carriage returns in CRLF files.
 */
export function stripShebang(source) {
  return source.replace(/^(?:\uFEFF)?#![^\r\n]*(?:\r\n|\r|\n|$)/, '');
}
