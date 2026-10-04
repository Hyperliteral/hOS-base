#include <fnmatch.h>
#include <string.h>

// Byte-oriented filename matching for the C locale (no multibyte, no
// locale collation): ?, *, [...] with negation ([!...]) and backslash
// escapes outside brackets, FNM_PATHNAME/FNM_PERIOD/FNM_NOESCAPE honored.
// Everything bash and ncurses ask of fnmatch lives in this subset.
int fnmatch(const char *pattern, const char *string, int flags)
{
	const char *p = pattern;
	const char *s = string;
	const char *star_p = 0;
	const char *star_s = 0;
	int noesc = flags & FNM_NOESCAPE;
	int pathname = flags & FNM_PATHNAME;
	int period = flags & FNM_PERIOD;

	// Leading-period rule: a period at the start of the string (or right
	// after a slash) only matches a literal period in the pattern.
	for (;;) {
		if (*p == '*') {
			// Collapse runs; a trailing star matches everything
			// left (subject to the period rule checked below).
			while (*p == '*')
				p++;
			if (!*p) {
				if (period && *s == '.' &&
				    (s == string || (pathname && s[-1] == '/')))
					return FNM_NOMATCH;
				if (pathname && strchr(s, '/'))
					return FNM_NOMATCH;
				return 0;
			}
			star_p = p;
			star_s = s;
			continue;
		}
		if (!*s)
			break;
		if (*p == '?' && !(period && *s == '.' &&
		    (s == string || (pathname && s[-1] == '/')))) {
			if (pathname && *s == '/')
				goto backtrack;
			p++;
			s++;
			continue;
		}
		if (*p == '[') {
			int neg = 0;
			int matched = 0;
			p++;
			if (*p == '!' || *p == '^') {
				neg = 1;
				p++;
			}
			if (*p == '/' && pathname)
				goto backtrack;
			if (period && *s == '.' &&
			    (s == string || (pathname && s[-1] == '/')) &&
			    *p != '.')
				goto backtrack;
			char prev = 0;
			int has_prev = 0;
			while (*p && *p != ']') {
				char c = *p;
				if (!noesc && c == '\\' && p[1]) {
					c = *++p;
					if (*s == c)
						matched = 1;
					prev = c;
					has_prev = 1;
					p++;
					continue;
				}
				if (c == '-' && has_prev && p[1] && p[1] != ']') {
					char hi = p[1];
					if (!noesc && hi == '\\' && p[2])
						hi = p[2];
					if (*s >= prev && *s <= hi)
						matched = 1;
					p += (hi != p[1]) ? 3 : 2;
					has_prev = 0;
					continue;
				}
				if (*s == c)
					matched = 1;
				prev = c;
				has_prev = 1;
				p++;
			}
			if (!*p)
				return FNM_NOMATCH; // unterminated class
			p++; // skip ']'
			if (matched == neg)
				goto backtrack;
			s++;
			continue;
		}
		char pc = *p;
		if (!noesc && pc == '\\' && p[1]) {
			p++;
			pc = *p;
		}
		if (pathname && pc == '/' && *s != '/')
			goto backtrack;
		if (pathname && *s == '/' && pc != '/')
			goto backtrack;
		if (*s != pc)
			goto backtrack;
		p++;
		s++;
		continue;
	backtrack:
		if (!star_p)
			return FNM_NOMATCH;
		// A star never crosses '/' under PATHNAME, and never matches a
		// leading period: give up when the retry point hits either.
		if (pathname && *star_s == '/')
			return FNM_NOMATCH;
		if (period && *star_s == '.' &&
		    (star_s == string || (pathname && star_s[-1] == '/')))
			return FNM_NOMATCH;
		star_s++;
		s = star_s;
		p = star_p;
	}
	while (*p == '*')
		p++;
	return *p ? FNM_NOMATCH : (*s ? FNM_NOMATCH : 0);
}
