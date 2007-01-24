/***************************************************************************
 *   eix is a small utility for searching ebuilds in the                   *
 *   Gentoo Linux portage system. It uses indexing to allow quick searches *
 *   in package descriptions with regular expressions.                     *
 *                                                                         *
 *   https://sourceforge.net/projects/eix                                  *
 *                                                                         *
 *   Copyright (c)                                                         *
 *     Wolfgang Frisch <xororand@users.sourceforge.net>                    *
 *     Emil Beinroth <emilbeinroth@gmx.net>                                *
 *     Martin V�th <vaeth@mathematik.uni-wuerzburg.de>                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include "eixrc.h"
#include <eixTk/exceptions.h>
#include <varsreader.h>

#define EIX_USERRC   "/.eixrc"

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif /* SYSCONFDIR */

#define EIX_SYSTEMRC SYSCONFDIR"/eixrc"

using namespace std;

EixRcOption::EixRcOption(char t, std::string name, std::string val, std::string desc) {
	type = t;
	key = name;
	if(type == LOCAL) {
		local_value = val;
	}
	else {
		value = val;
		description = desc;
	}
}

bool
EixRc::getRedundantFlagAtom(const char *s, Keywords::Redundant type, RedAtom &r)
{
	r.only &= ~type;
	if(s == NULL)
	{
		r.red &= ~type;
		return true;
	}
	if(*s == '+')
	{
		s++;
		r.only |= type;
		r.oins |= type;
	}
	else if(*s == '-')
	{
		s++;
		r.only |= type;
		r.oins &= ~type;
	}
	if((strcasecmp(s, "no") == 0) ||
	   (strcasecmp(s, "false") == 0))
	{
		r.red &= ~type;
	}
	else if(strcasecmp(s, "some") == 0)
	{
		r.red |= type;
		r.all &= ~type;
		r.spc &= ~type;
	}
	else if(strcasecmp(s, "some-installed") == 0)
	{
		r.red |= type;
		r.all &= ~type;
		r.spc |= type;
		r.ins |= type;
	}
	else if(strcasecmp(s, "some-uninstalled") == 0)
	{
		r.red |= type;
		r.all &= ~type;
		r.spc |= type;
		r.ins &= ~type;
	}
	else if(strcasecmp(s, "all") == 0)
	{
		r.red |= type;
		r.all |= type;
		r.spc &= ~type;
	}
	else if(strcasecmp(s, "all-installed") == 0)
	{
		r.red |= type;
		r.all |= type;
		r.spc |= type;
		r.ins |= type;
	}
	else if(strcasecmp(s, "all-uninstalled") == 0)
	{
		r.red |= type;
		r.all |= type;
		r.spc |= type;
		r.ins &= ~type;
	}
	else
		return false;
	return true;
}

void EixRc::read()
{
	eprefix = getenv("EPREFIX");
	if(eprefix)
		m_eprefix = eprefix;
	else
		m_eprefix = "";
	const char *configroot = getenv("PORTAGE_CONFIGROOT");
	if(configroot)
		m_eprefixconf = m_eprefix + configroot;
	else
		m_eprefixconf = m_eprefix;

	set<string> has_reference;

	// First, we create defaults and the main map with all variables
	// (including all values required by delayed references).
	read_undelayed(has_reference);

	// Resolve delayed references recursively.
	for(default_index i = 0; i < defaults.size(); ++i)
	{
		set<string> visited;
		const char *errtext;
		string errvar;
		if(resolve_delayed_recurse(defaults[i].key, visited,
			has_reference, &errtext, &errvar) == NULL)
		{
			cerr << "fatal config error: " << errtext
				<< " in delayed substitution of " << errvar
				<< "\n";
			exit(2);
		}
	}

	// Let %%{ expand to %{
	for(map<string,string>::iterator it = begin(); it != end(); ++it)
	{
		string &str = it->second;
		string::size_type pos = 0;
		for(;; pos += 2)
		{
			pos = str.find("%%{", pos);
			if(pos == string::npos)
				break;
			str.erase(pos,1);
		}
	}

	// set m_eprefix/m_eprefixconf/eprefix to possibly new settings:
	m_eprefix = (*this)["EPREFIX"];
	m_eprefixconf = (*this)["PORTAGE_CONFIGROOT"] + m_eprefix;
	// Important: eprefix should remain NULL if it is not in environment
	// and not set different from "" in the eixrc files.
	if(!m_eprefix.empty())
		eprefix = m_eprefix.c_str();
}

string *EixRc::resolve_delayed_recurse(string key, set<string> &visited, set<string> &has_reference, const char **errtext, string *errvar)
{
	string *value = &((*this)[key]);
	if(has_reference.find(key) == has_reference.end())
		return value;
	string::size_type pos = 0;
	for(;;)
	{
		string::size_type length;
		DelayedType type = find_next_delayed(*value, &pos, &length);
		if(type == DelayedNotFound) {
			has_reference.erase(key);
			return value;
		}
		if(type == DelayedFi) {
			*errtext = "FI without IF";
			*errvar = key;
			return NULL;
		}
		if(type == DelayedElse) {
			*errtext = "ELSE without IF";
			*errvar = key;
			return NULL;
		}
		bool will_test = false;
		string::size_type varpos = pos + 2;
		string::size_type varlength = length - 3;
		if((type == DelayedIf) || (type == DelayedNotif)) {
			will_test = true;
			varpos++;
			varlength--;
		}
		if(visited.find(key) != visited.end()) {
			*errtext = "self-reference";
			*errvar = key;
			return NULL;
		}
		visited.insert(key);
		string *s = resolve_delayed_recurse(
			( ((*value)[varpos] == '*') ?
			(varprefix + value->substr(varpos + 1, varlength - 1)) :
			value->substr(varpos, varlength)),
			visited, has_reference, errtext, errvar);
		visited.erase(key);
		if(!s)
			return NULL;
		if(! will_test) {
			value->replace(pos, length, *s);
			pos += s->length();
			continue;
		}
		string::size_type skippos = pos;
		bool result = istrue(s->c_str()) ?
			(type == DelayedIf) : (type == DelayedNotif);
		string::size_type delpos = string::npos;
		if(result)
			value->erase(skippos, length);
		else {
			delpos = skippos;
			skippos += length;
		}
		bool gotelse = false;
		unsigned int count = 0;
		for(;; skippos += length)
		{
			type = find_next_delayed(*value, &skippos, &length);

			if(type == DelayedFi) {
				if(count --)
					continue;
				if(delpos == string::npos)
					value->erase(skippos, length);
				else
					value->erase(delpos,
						(skippos + length) - delpos);
				break;
			}
			if(type == DelayedElse) {
				if(count)
					continue;
				if(gotelse) {
					*errtext = "double ELSE";
					*errvar = key;
					return NULL;
				}
				gotelse = true;
				if(result) {
					value->erase(skippos, length);
					length = 0;
					delpos = skippos;
					continue;
				}
				value->erase(delpos,
					(skippos + length) - delpos);
				skippos = delpos;
				length = 0;
				delpos = string::npos;
				continue;
			}
			if((type == DelayedIf) || (type == DelayedNotif)) {
				count ++;
				continue;
			}
			if(type == DelayedNotFound) {
				*errtext = "IF without FI";
				*errvar = key;
				return NULL;
			}
		}
	}
}

/** Create defaults and the main map with all variables
   (including all values required by delayed references).
   @arg has_reference is initialized to corresponding keys */
void EixRc::read_undelayed(set<string> &has_reference) {
	map<string,string> tempmap;
	set<string> default_keys;

	// Initialize with the default variables
	for(default_index i = 0; i < defaults.size(); ++i) {
		default_keys.insert(defaults[i].key);
		tempmap[defaults[i].key] = defaults[i].value;
	}

	// override with EIX_SYSTEMRC
	VarsReader rc(//VarsReader::NONE
			VarsReader::SUBST_VARS
			|VarsReader::ALLOW_SOURCE
			|VarsReader::INTO_MAP);
	rc.useMap(&tempmap);
	rc.read((m_eprefixconf + EIX_SYSTEMRC).c_str());

	// override with EIX_USERRC
	char *home = getenv("HOME");
	if(!home)
		WARNING("No $HOME found in environment.");
	else
	{
		string eixrc(home);
		eixrc.append(EIX_USERRC);
		rc.read(eixrc.c_str());
	}

	// override with ENV
	for(map<string,string>::iterator it = tempmap.begin();
		it != tempmap.end(); ++it)
	{
		char *val = getenv((it->first).c_str());
		if(val)
			it->second = string(val);
	}

	// Set new values as default and for printing with --dump.
	for(vector<EixRcOption>::iterator it = defaults.begin();
		it != defaults.end(); ++it) {
		it->local_value = tempmap[it->key];
		(*this)[it->key] = it->local_value;
	}

	// Recursively join all delayed references to defaults,
	// keeping main map up to date. Also initialize has_reference.
	for(default_index i = 0; i < defaults.size(); ++i)
	{
		string &str = defaults[i].local_value;
		string::size_type pos = 0;
		string::size_type length = 0;
		for(;; pos += length)
		{
			DelayedType type = find_next_delayed(str, &pos, &length);
			if (type == DelayedNotFound)
				break;
			else if (type == DelayedVariable) {
				pos += 2;
				length -= 2;
			}
			else if ((type == DelayedIf) || (type == DelayedNotif)) {
				pos += 3;
				length -= 3;
			}
			else
				continue;
			has_reference.insert(defaults[i].key);
			if(str[pos] == '*') {
				string s = str.substr(pos + 1, length - 2);
				join_delayed(string(EIX_VARS_PREFIX) + s,
					default_keys, tempmap);
				join_delayed(string(DIFF_EIX_VARS_PREFIX) + s,
					default_keys, tempmap);
			}
			else {
				join_delayed(str.substr(pos, length - 1),
					default_keys, tempmap);
			}
		}
	}
}

void EixRc::join_delayed(const string &key, set<string> &default_keys, const map<string,string> &tempmap)
{
	if(default_keys.find(key) != default_keys.end())
		return;
	string val;
	map<string,string>::const_iterator f = tempmap.find(key);
	if(f != tempmap.end()) {
	// Note that if a variable is defined in a file and in ENV,
	// its value was already overridden from ENV.
		val = f->second;
	}
	else {
	// If it was not defined in a file, it might be in ENV anyway:
		char *envval = getenv(key.c_str());
		if(envval)
			val = string(envval);
	}
	defaults.push_back(EixRcOption(EixRcOption::LOCAL, key, val, ""));
	default_keys.insert(key);
	(*this)[key] = val;
}

EixRc::DelayedType EixRc::find_next_delayed(const string &str, string::size_type *posref, string::size_type *length)
{
	string::size_type pos = *posref;
	for(;; pos += 2)
	{
		pos = str.find("%{", pos);
		if(pos == string::npos)
		{
			*posref = string::npos;
			return DelayedNotFound;
		}
		if(pos > 0) {
			if(str[pos - 1] == '%')
				continue;
		}
		string::size_type i = pos + 2;
		char c = str[i++];
		DelayedType type;
		if(c == '}')
			type = DelayedFi;
		else
		{
			if(c == '?') {
				type = DelayedIf;
				c = str[i++];
			}
			else if(c == '!') {
				type = DelayedNotif;
				c = str[i++];
			}
			else
				type = DelayedVariable;
			if((c != '*') && (c != '_') &&
				((c < 'A') || (c > 'Z')) &&
				((c < 'a') || (c > 'z')))
				continue;
			for(;;)
			{
				c = str[i++];
				if ((c != '_') &&
					((c < '0') || (c > '9')) &&
					((c < 'A') || (c > 'Z')) &&
					((c < 'a') || (c > 'z')))
					break;
			}
			if(c != '}')
				continue;
			if(strcasecmp(
				(str.substr(pos + 2, i - pos - 3)).c_str(),
				"else") == 0)
				type = DelayedElse;
		}
		*posref = pos;
		if(length)
			*length = i - pos;
		return type;
	}
}

void EixRc::clear()
{
	defaults.clear();
	((map<string,string>*) this)->clear();
}

void EixRc::addDefault(EixRcOption option)
{
	defaults.push_back(option);
}

bool EixRc::istrue(const char *s)
{
	if(strcasecmp(s, "true") == 0)
		return true;
	if(strcasecmp(s, "1") == 0)
		return true;
	if(strcasecmp(s, "yes") == 0)
		return true;
	if(strcasecmp(s, "y") == 0)
		return true;
	if(strcasecmp(s, "on") == 0)
		return true;
	return false;
}

void EixRc::getRedundantFlags(const char *key,
	Keywords::Redundant type,
	RedPair &p)
{
	string value=(*this)[key].c_str();
	vector<string> a=split_string(value);

	for(;;)// a dummy loop for break on errors
	{
		vector<string>::iterator it = a.begin();
		if(it == a.end())
			break;
		if(!getRedundantFlagAtom(it->c_str(), type, p.first))
			break;
		++it;
		if(it == a.end())
		{
			getRedundantFlagAtom(NULL, type, p.second);
			return;
		}
		const char *s = it->c_str();
		if((strcasecmp(s, "or") == 0) ||
			(strcasecmp(s, "||") == 0) ||
			(strcasecmp(s, "|") == 0))
		{
			++it;
			if(it == a.end())
				break;
			s = it->c_str();
		}
		if(!getRedundantFlagAtom(s, type, p.first))
			break;
		++it;
		if(it == a.end())
			return;
		break;
	}
	WARNING("%s has unknown value \"%s\";\n"
		"\tassuming value \"all-installed\" instead.",
		key, value.c_str());
	getRedundantFlagAtom("all-installed", type, p.first);
	getRedundantFlagAtom(NULL, type, p.second);
}

int EixRc::getInteger(const char *key)
{
	return atoi((*this)[key].c_str());
}

string EixRc::as_comment(const char *s)
{
	string ret = s;
	string::size_type pos = 0;
	while(pos = ret.find("\n", pos), pos != string::npos) {
		ret.insert(pos + 1, "# ");
		pos += 2;
	}
	return ret;
}


void EixRc::dumpDefaults(FILE *s, bool use_defaults)
{
	const char *message = use_defaults ?
		"was locally changed to:" :
		"changed locally, default was:";
	for(vector<EixRcOption>::size_type i = 0;
		i < defaults.size();
		++i)
	{
		const char *typestring = "UNKNOWN";
		switch(defaults[i].type) {
			case EixRcOption::BOOLEAN: typestring = "BOOLEAN";
						  break;
			case EixRcOption::STRING: typestring = "STRING";
						  break;
			case EixRcOption::INTEGER: typestring = "INTEGER";
						  break;
			case EixRcOption::LOCAL: typestring = NULL;
		}
		const char *key   = defaults[i].key.c_str();
		const char *value = defaults[i].local_value.c_str();
		if(!typestring) {
			fprintf(s,
				"# locally added:\n%s='%s'\n\n",
				key, value);
			continue;
		}
		const char *deflt = defaults[i].value.c_str();
		const char *output = (use_defaults ? deflt : value);
		const char *comment = (use_defaults ? value : deflt);
		fprintf(s,
				"# %s\n"
				"# %s\n"
				"%s='%s'\n",
				as_comment(typestring).c_str(),
				as_comment(defaults[i].description.c_str()).c_str(),
				key,
				output);
		if(strcmp(deflt,value) == 0)
			fprintf(s, "\n");
		else {
			fprintf(s,
				"# %s\n"
				"# %s='%s'\n\n",
				message,
				key,
				as_comment(comment).c_str());
		}
	}
}
