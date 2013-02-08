// vim:set noet cinoptions= sw=4 ts=4:
// This file is part of the eix project and distributed under the
// terms of the GNU General Public License v2.
//
// Copyright (c)
//   Wolfgang Frisch <xororand@users.sourceforge.net>
//   Emil Beinroth <emilbeinroth@gmx.net>
//   Martin Väth <vaeth@mathematik.uni-wuerzburg.de>

#include <config.h>

#ifndef HAVE_STRNDUP
#include <sys/types.h>
#endif

#include <fnmatch.h>

#include <locale>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "eixTk/diagnostics.h"
#include "eixTk/formated.h"
#include "eixTk/i18n.h"
#include "eixTk/likely.h"
#include "eixTk/null.h"
#include "eixTk/stringutils.h"

using std::map;
using std::set;
using std::string;
using std::vector;

using std::cerr;
using std::cout;
using std::endl;

const char *spaces(" \t\r\n");
const char *doublequotes("\"$\\");

std::locale localeC("C");

static void erase_escapes(string *s, const char *at) ATTRIBUTE_NONNULL_;
template <typename T> inline static void split_string_template(T *vec, const string &str, const bool handle_escape, const char *at, const bool ignore_empty) ATTRIBUTE_NONNULL_;
template <typename T> inline static void join_to_string_template(string *s, const T &vec, const string &glue) ATTRIBUTE_NONNULL_;

#ifndef HAVE_STRNDUP
/* If we don't have strndup, we use our own ..
 * darwin (macos) doesn't have strndup, it's a GNU extension
 * See http://bugs.gentoo.org/show_bug.cgi?id=111912 */

char *
strndup(const char *s, size_t n)
{
	const char *p(s);
	while(likely(*p++ && n--)) {}
	n = p - s - 1;
	char *r(static_cast<char *>(malloc(n + 1)));
	if(r) {
		memcpy(r, s, n);
		r[n] = 0;
	}
	return r;
}
#endif /* HAVE_STRNDUP */

/** Check string if it only contains digits. */
bool
is_numeric(const char *str)
{
	for(char c(*str); likely(c != '\0'); c = *(++str)) {
		if(!isdigit(c, localeC))
			return false;
	}
	return true;
}

/** Add symbol if it is not already the last one */
void
optional_append(std::string *s, char symbol)
{
	if(s->empty() || ((*(s->rbegin()) != symbol)))
		s->append(1, symbol);
}

/** Trim characters on left side of string.
 * @param str String that should be trimmed
 * @param delims characters that should me removed */
void
ltrim(std::string *str, const char *delims)
{
	// trim leading whitespace
	std::string::size_type notwhite(str->find_first_not_of(delims));
	if(notwhite != std::string::npos)
		str->erase(0, notwhite);
	else
		str->clear();
}

/** Trim characters on right side of string.
 * @param str String that should be trimmed
 * @param delims characters that should me removed */
void
rtrim(std::string *str, const char *delims)
{
	// trim trailing whitespace
	std::string::size_type notwhite(str->find_last_not_of(delims));
	if(notwhite != std::string::npos)
		str->erase(notwhite+1);
	else
		str->clear();
}

/** Trim characters on left and right side of string.
 * @param str String that should be trimmed
 * @param delims characters that should me removed */
void
trim(string *str, const char *delims)
{
	ltrim(str, delims);
	rtrim(str, delims);
}

/** Trim characters on left and right side of string.
 * @param str String that should be trimmed
 * @param delims characters that should me removed */
void
trimall(string *str, const char *delims, char c)
{
	string::size_type pos(0);
	while(unlikely((pos = str->find_first_of(delims, pos)) != string::npos)) {
		string::size_type end(str->find_first_not_of(spaces, pos + 1));
		if(end == string::npos) {
			str->erase(pos);
			return;
		}
		if(pos != 0) {
			(*str)[pos] = c;
			if(++pos == end) {
				continue;
			}
		}
		str->erase(pos, end - pos);
	}
}

/** Check if slot contains a subslot and if yes, split it away.
    Also turn slot "0" into nothing */
bool
slot_subslot(string *slot, string *subslot)
{
	string::size_type sep(slot->find('/'));
	if(sep == string::npos) {
		subslot->clear();
		if((*slot) == "0") {
			slot->clear();
		}
		return false;
	}
	subslot->assign(*slot, sep + 1, string::npos);
	slot->resize(sep);
	if(*slot == "0") {
		slot->clear();
	}
	return true;
}

/** Split full to slot and subslot. Also turn slot "0" into nothing
 * @return true if subslot exists */
bool
slot_subslot(const string &full, string *slot, string *subslot)
{
	string::size_type sep(full.find('/'));
	if(sep == string::npos) {
		if(full != "0") {
			*slot = full;
		} else {
			slot->clear();
		}
		subslot->clear();
		return false;
	}
	subslot->assign(full, sep + 1, string::npos);
	slot->assign(full, 0, sep);
	if((*slot) == "0") {
		slot->clear();
	}
	return true;
}

const char *
ExplodeAtom::get_start_of_version(const char *str, bool allow_star)
{
	// There must be at least one symbol before the version:
	if(unlikely(*(str++) == '\0'))
		return NULLPTR;
	const char *x(NULLPTR);
	while(likely((*str != '\0') && (*str != ':'))) {
		if(unlikely(*str++ == '-')) {
			if(isdigit(*str, localeC) ||
				unlikely(allow_star && ((*str) == '*'))) {
				x = str;
			}
		}
	}
	return x;
}

char *
ExplodeAtom::split_version(const char *str)
{
	const char *x(get_start_of_version(str, false));
	if(likely(x != NULLPTR))
		return strdup(x);
	return NULLPTR;
}

char *
ExplodeAtom::split_name(const char *str)
{
	const char *x(get_start_of_version(str, false));
	if(likely(x != NULLPTR)) {
GCC_DIAG_OFF(sign-conversion)
		return strndup(str, ((x - 1) - str));
GCC_DIAG_ON(sign-conversion)
	}
	return NULLPTR;
}

char **
ExplodeAtom::split(const char *str)
{
	static char* out[2] = { NULLPTR, NULLPTR };
	const char *x(get_start_of_version(str, false));

	if(unlikely(x == NULLPTR))
		return NULLPTR;
GCC_DIAG_OFF(sign-conversion)
	out[0] = strndup(str, ((x - 1) - str));
GCC_DIAG_ON(sign-conversion)
	out[1] = strdup(x);
	return out;
}

string
to_lower(const string &str)
{
	string::size_type s(str.size());
	string res;
	for(string::size_type c(0); c != s; ++c) {
		res.append(1, tolower(str[c], localeC));
	}
	return res;
}

char
get_escape(char c)
{
	switch(c) {
		case 0:
		case '\\': return '\\';
		case 'n':  return '\n';
		case 'r':  return '\r';
		case 't':  return '\t';
		case 'b':  return '\b';
		case 'a':  return '\a';
		default:
			break;
	}
	return c;
}

void
unescape_string(string *str)
{
	string::size_type pos(0);
	while(unlikely((pos = str->find('\\', pos)) != string::npos)) {
		string::size_type p(pos + 1);
		if(p == str->size())
			return;
		str->replace(pos, 2, 1, get_escape((*str)[p]));
	}
}

void
escape_string(string *str, const char *at)
{
	string my_at(at);
	my_at.append("\\");
	string::size_type pos(0);
	while(unlikely((pos = str->find_first_of(my_at, pos)) != string::npos)) {
		str->insert(pos, 1, '\\');
		pos += 2;
	}
}

static void
erase_escapes(string *s, const char *at)
{
	string::size_type pos(0);
	while((pos = s->find('\\', pos)) != string::npos) {
		++pos;
		if(pos == s->size()) {
			s->erase(pos - 1, 1);
			break;
		}
		char c((*s)[pos]);
		if((c == '\\') || (strchr(at, c) != NULLPTR))
			s->erase(pos - 1, 1);
	}
}

template <typename T>
inline static void
split_string_template(T *vec, const string &str, const bool handle_escape, const char *at, const bool ignore_empty)
{
	string::size_type last_pos(0), pos(0);
	while((pos = str.find_first_of(at, pos)) != string::npos) {
		if(unlikely(handle_escape)) {
			bool escaped(false);
			string::size_type s(pos);
			while(s > 0) {
				if(str[--s] != '\\')
					break;
				escaped = !escaped;
			}
			if(escaped) {
				++pos;
				continue;
			}
			string r(str, last_pos, pos - last_pos);
			erase_escapes(&r, at);
			if(likely((!r.empty()) || !ignore_empty))
				push_back(vec, r);
		} else if(likely((pos > last_pos) || !ignore_empty)) {
			push_back(vec, str.substr(last_pos, pos - last_pos));
		}
		last_pos = ++pos;
	}
	if(unlikely(handle_escape)) {
		string r(str, last_pos);
		erase_escapes(&r, at);
		if(likely((!r.empty()) || !ignore_empty))
			push_back(vec, r);
	} else if(likely((str.size() > last_pos) || !ignore_empty)) {
		push_back(vec, str.substr(last_pos));
	}
}

void
split_string(vector<string> *vec, const string &str, const bool handle_escape, const char *at, const bool ignore_empty)
{ split_string_template< vector<string> >(vec, str, handle_escape, at, ignore_empty); }

void
split_string(set<string> *vec, const string &str, const bool handle_escape, const char *at, const bool ignore_empty)
{ split_string_template< set<string> >(vec, str, handle_escape, at, ignore_empty); }

vector<string>
split_string(const string &str, const bool handle_escape, const char *at, const bool ignore_empty)
{
	std::vector<std::string> vec;
	split_string(&vec, str, handle_escape, at, ignore_empty);
	return vec;
}

/** Calls split_string() with a vector and then join_to_string().
 * @param source string to split
 * @param dest   result. May be identical to source. */
void
split_and_join(string *dest, const string &source, const string &glue, const bool handle_escape, const char *at, const bool ignore_empty)
{
	vector<string> vec;
	split_string(&vec, source, handle_escape, at, ignore_empty);
	join_to_string(dest, vec, glue);
}

/** Calls split_string() with a vector and then join_to_string().
 * @param source string to split
 * @return result. */
string
split_and_join_string(const string &source, const string &glue, const bool handle_escape, const char *at, const bool ignore_empty)
{
	string r;
	split_and_join(&r, source, glue, handle_escape, at, ignore_empty);
	return r;
}

/** Resolve a string of -/+ keywords to a set of actually set keywords */
bool
resolve_plus_minus(set<string> *s, const string &str, const set<string> *warnignore)
{
	vector<string> l;
	split_string(&l, str);
	return resolve_plus_minus(s, l, warnignore);
}

template <typename T>
inline static void
join_to_string_template(string *s, const T &vec, const string &glue)
{
	for(typename T::const_iterator it(vec.begin()); likely(it != vec.end()); ++it) {
		if(likely(!s->empty())) {
			s->append(glue);
		}
		s->append(*it);
	}
}

void
join_to_string(string *s, const vector<string> &vec, const string &glue)
{ join_to_string_template< vector<string> >(s, vec, glue); }

void
join_to_string(string *s, const set<string> &vec, const string &glue)
{ join_to_string_template< set<string> >(s, vec, glue); }

bool
resolve_plus_minus(set<string> *s, const vector<string> &l, const set<string> *warnignore)
{
	bool minuskeyword(false);
	for(vector<string>::const_iterator it(l.begin()); likely(it != l.end()); ++it) {
		if(unlikely(it->empty())) {
			continue;
		}
		if(unlikely((*it)[0] == '+')) {
			cerr << eix::format(_("flags should not start with a '+': %s")) % *it
				<< endl;
			s->insert(it->substr(1));
			continue;
		}
		if(unlikely((*it)[0] == '-')) {
			if(*it == "-*") {
				s->clear();
				continue;
			}
			if(*it == "-~*") {
				vector<string> v;
				make_vector(&v, *s);
				for(vector<string>::iterator i(v.begin());
					unlikely(i != v.end()); ++i) {
					if((i->size() >=2) && ((*i)[0] == '~')) {
						s->erase(*i);
					}
				}
			}
			string key(*it, 1);
			if(s->erase(key)) {
				continue;
			}
			if(warnignore != NULLPTR) {
				if(warnignore->find(key) == warnignore->end()) {
					minuskeyword = true;
				}
			} else {
				minuskeyword = true;
			}
		}
		s->insert(*it);
	}
	return minuskeyword;
}

void
StringHash::store_string(const string &s)
{
	if(finalized) {
		fprintf(stderr, _("Internal error: Storing required after finalizing"));
		exit(EXIT_FAILURE);
	}
	push_back(s);
}

void
StringHash::hash_string(const string &s)
{
	if(finalized) {
		fprintf(stderr, _("Internal error: Hashing required after finalizing"));
		exit(EXIT_FAILURE);
	}
	if(!hashing) {
		fprintf(stderr, _("Internal error: Hashing required in non-hash mode"));
		exit(EXIT_FAILURE);
	}
	map<string, StringHash::size_type>::const_iterator i(str_map.find(s));
	if(i != str_map.end())
		return;
	// For the moment, use str_map only as a set: Wait for finalize()
	str_map[s] = 0;  // size();
	// store_string(s);
}

void
StringHash::store_words(const vector<string> &v)
{
	for(vector<string>::const_iterator i(v.begin()); likely(i != v.end()); ++i) {
		store_string(*i);
	}
}

void
StringHash::hash_words(const vector<string> &v)
{
	for(vector<string>::const_iterator i(v.begin()); likely(i != v.end()); ++i)
		hash_string(*i);
}

StringHash::size_type
StringHash::get_index(const string &s) const
{
	if(!finalized) {
		cerr << _("Internal error: Index required before sorting.") << endl;
		exit(EXIT_FAILURE);
	}
	map<string, StringHash::size_type>::const_iterator i(str_map.find(s));
	if(i == str_map.end()) {
		cerr << _("Internal error: Trying to shortcut non-hashed string.") << endl;
		exit(EXIT_FAILURE);
	}
	return i->second;
}

const string&
StringHash::operator[](StringHash::size_type i) const
{
	if(i >= size()) {
		cerr << _("Database corrupt: Nonexistent hash required");
		exit(EXIT_FAILURE);
	}
	return vector<string>::operator[](i);
}

void
StringHash::output(const set<string> *skip) const
{
	for(vector<string>::const_iterator i(begin()); likely(i != end()); ++i) {
		if(skip != NULLPTR) {
			if(skip->find(*i) != skip->end()) {
				continue;
			}
		}
		cout << *i << "\n";
	}
}

void
StringHash::finalize()
{
	if(finalized)
		return;
	finalized = true;
	if(!hashing)
		return;
	clear();
	for(map<string, size_type>::iterator it(str_map.begin());
		likely(it != str_map.end()); ++it) {
		it->second = size();
		push_back(it->first);
	}
}

bool
match_list(const char **str_list, const char *str)
{
	if(str_list != NULLPTR) {
		while(likely(*str_list != NULLPTR)) {
			if(fnmatch(*(str_list++), str, 0) == 0) {
				return true;
			}
		}
	}
	return false;
}
