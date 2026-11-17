// Sharing a theme: where the themes repository and its site are, and the link the Share button opens.
// Pure: no DOM, so the tests can assert on strings.

export const THEMES_REPO = 'Em3ritus/umbravox-themes';
export const THEMES_SITE = 'https://em3ritus.github.io/umbravox-themes/';

// A theme's folder name, the same rule the device applies to a pack folder.
export const FOLDER_RE = /^[A-Za-z0-9_-]{1,31}$/;

// The submission form with its fields filled. The keys are the field ids in the themes repository's
// .github/ISSUE_TEMPLATE/submit-theme.yml. A page cannot attach the zip: the person drags it into the form.
export function shareUrl(theme, basedOn = '') {
  const name = String((theme && theme.name) || '');
  const q = new URLSearchParams({ template: 'submit-theme.yml', title: 'Theme: ' + name, 'theme-name': name });
  const author = String((theme && theme.author) || '');
  if (author) q.set('author', author);
  if (FOLDER_RE.test(String(basedOn))) q.set('based-on', String(basedOn));
  return 'https://github.com/' + THEMES_REPO + '/issues/new?' + q.toString();
}
