/*
 * DHTValues.cc
 * Save and restore of atom values.
 *
 * Copyright (c) 2008,2009,2013,2017,2019 Linas Vepstas <linas@linas.org>
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <opencog/atoms/base/Atom.h>
#include <opencog/atoms/value/FloatValue.h>
#include <opencog/atoms/value/LinkValue.h>
#include <opencog/atoms/value/StringValue.h>
#include <opencog/atoms/base/Valuation.h>
#include <opencog/atoms/truthvalue/CountTruthValue.h>
#include <opencog/atoms/truthvalue/SimpleTruthValue.h>
#include <opencog/atoms/truthvalue/TruthValue.h>

#include "DHTAtomStorage.h"

using namespace opencog;

/* ================================================================== */

/// Store ALL of the values associated with the atom.
void DHTAtomStorage::store_atom_values(const Handle& atom)
{
	if (_observing_only)
		throw IOException(TRACE_INFO, "DHT Node is only observing!");

	dht::InfoHash muid = get_membership(atom);

	// If there are no keys currently on the atom, but there are values
	// in the DHT, then we need to clobber the values in the DHT.  Try
	// to avoid having to to a put, by doing a get first.  Maybe we can
	// get more efficient by caching?
	if (0 == atom->getKeys().size())
	{
		auto dvals = get_stuff(muid, _values_filter);
		if (0 < dvals.size())
			delete_atom_values(atom);
		return;
	}

	// Make sure all of the keys appear in the AtomSpace
	for (const Handle& key : atom->getKeys())
		store_recursive(key);

	// Attach the value to the atom
	_runner.put(muid,
		dht::Value(_values_policy, encodeValuesToAlist(atom), 1));

	_value_updates ++;
}

/* ================================================================== */

/// Delete ALL of the values associated with the atom.
void DHTAtomStorage::delete_atom_values(const Handle& atom)
{
	if (_observing_only)
		throw IOException(TRACE_INFO, "DHT Node is only observing!");

	// Attach the value to the atom
	dht::InfoHash muid = get_membership(atom);
	_runner.put(muid, dht::Value(_values_policy, "", 1));

	_value_deletes ++;
}

/* ================================================================ */

Handle DHTAtomStorage::fetch_values(Handle&& h)
{
	dht::InfoHash muid = get_membership(h);

	auto dvals = get_stuff(muid, _values_filter);

	// There may be multiple values attached to this Atom.
	// We only want one; the one with the latest timestamp.
	unsigned long timestamp = 0;
	std::string alist;
	for (const auto& dval : dvals)
	{
		// std::cout << "Got value: " << dval->toString() << std::endl;
		if (timestamp < dval->id)
		{
			timestamp = dval->id;
			alist = dval->unpack<std::string>();
		}
	}
	// std::cout << "Latest svalue: " << alist << std::endl;
	decodeAlist(h, alist);
	_value_fetches++;

	return h;
}

/* ================================================================== */

/// Convert value (or Atom) into a string.
std::string DHTAtomStorage::encodeValueToStr(const ValuePtr& v)
{
	if (nameserver().isA(v->get_type(), FLOAT_VALUE))
	{
		// The FloatValue to_string() print prints out a high-precision
		// form of the value, as compared to SimpleTruthValue, which
		// only prints 6 digits and breaks the unit tests.
		FloatValuePtr fv(FloatValueCast(v));
		return fv->FloatValue::to_string();
	}
	return v->to_short_string();
}

/* ================================================================== */

std::string DHTAtomStorage::encodeValuesToAlist(const Handle& h)
{
	std::stringstream rv;

	rv << "(";
	for (const Handle& k: h->getKeys())
	{
		ValuePtr p = h->getValue(k);
		rv << "(" << k->to_short_string()
		   << " . " << encodeValueToStr(p) + ")";
	}
	rv << ")";
	return rv.str();
}

/* ================================================================== */

/**
 * Return a Value correspnding to the input string.
 * It is assumed the input string is encoded as a scheme string.
 * For example, `(FloatValue 1 2 3 4)`
 *
 * XXX FIXME This needs to be fuzzed; it is very likely to crash
 * and/or contain bugs if it is given strings of unexpected formats.
 */
ValuePtr DHTAtomStorage::decodeStrValue(std::string& stv, size_t& pos)
{
	size_t totlen = stv.size();

#define LV "(LinkValue"
	if (0 == stv.compare(pos, sizeof(LV)-1, LV))
	{
		size_t vos = pos + sizeof(LV)-1;
		std::vector<ValuePtr> vv;
		vos = stv.find('(', vos);
		size_t epos = vos;
		size_t done = vos + 1;
		while (vos != std::string::npos and vos < done)
		{
			// Find the next balanced paren, and restart there.
			// This is not very efficient, but it works.
			epos = vos;
			int pcnt = 1;
			while (0 < pcnt and epos < totlen)
			{
				char c = stv[++epos];
				if ('(' == c) pcnt ++;
				else if (')' == c) pcnt--;
			}
			if (epos >= totlen)
				throw SyntaxException(TRACE_INFO,
					"Malformed LinkValue: %s", stv.substr(pos).c_str());

			vv.push_back(decodeStrValue(stv, vos));
			done = stv.find(')', epos+1);
			vos = stv.find('(', epos+1);
		}
		if (std::string::npos == done)
			throw SyntaxException(TRACE_INFO,
				"Malformed LinkValue: %s", stv.substr(pos).c_str());
		pos = done + 1;
		return createLinkValue(vv);
	}

#define FV "(FloatValue"
	if (0 == stv.compare(pos, sizeof(FV)-1, FV))
	{
		size_t vos = pos + sizeof(FV)-1;
		std::vector<double> fv;
		while (vos < totlen and stv[vos] != ')')
		{
			size_t epos;
			fv.push_back(stod(stv.substr(vos), &epos));
			vos += epos;
		}
		pos = vos + 1;
		return createFloatValue(fv);
	}

#define TVL "(SimpleTruthValue "
#define TVS "(stv "
	size_t vos = std::string::npos;
	if (0 == stv.compare(pos, sizeof(TVL)-1, TVL))
		vos = pos + sizeof(TVL) - 1;
	else
	if (0 == stv.compare(pos, sizeof(TVS)-1, TVS))
		vos = pos + sizeof(TVS) - 1;

	if (std::string::npos != vos)
	{
		size_t elen;
		double strength = stod(stv.substr(vos), &elen);
		vos += elen;
		double confidence = stod(stv.substr(vos), &elen);
		vos += elen;
		vos = stv.find(')', vos);
		if (std::string::npos == vos)
			throw SyntaxException(TRACE_INFO,
				"Malformed SimpleTruthValue: %s", stv.substr(pos).c_str());
		pos = vos + 1;
		return ValueCast(createSimpleTruthValue(strength, confidence));
	}

#define CTV "(CountTruthValue "
	if (0 == stv.compare(pos, sizeof(CTV)-1, CTV))
	{
		size_t vos = pos + sizeof(CTV) - 1;
		size_t elen;
		double strength = stod(stv.substr(vos), &elen);
		vos += elen;
		double confidence = stod(stv.substr(vos), &elen);
		vos += elen;
		double count = stod(stv.substr(vos), &elen);
		vos += elen;
		vos = stv.find(')', vos);
		if (std::string::npos == vos)
			throw SyntaxException(TRACE_INFO,
				"Malformed CountTruthValue: %s", stv.substr(pos).c_str());
		pos = vos + 1;
		return ValueCast(createCountTruthValue(strength, confidence, count));
	}

	// XXX FIXME this mishandles escaped quotes
#define SV "(StringValue"
	if (0 == stv.compare(pos, sizeof(SV)-1, SV))
	{
		size_t vos = pos + sizeof(SV) - 1;
		std::vector<std::string> sv;
		size_t epos = stv.find(')', vos+1);
		if (std::string::npos == epos)
			throw SyntaxException(TRACE_INFO,
				"Malformed StringValue: %s", stv.substr(pos).c_str());
		while (vos < epos)
		{
			vos = stv.find('\"', vos);
			if (std::string::npos == vos) break;
			size_t evos = stv.find('\"', vos+1);
			sv.push_back(stv.substr(vos+1, evos-vos-1));
			vos = evos+1;
		}
		pos = epos + 1;
		return createStringValue(sv);
	}

	throw SyntaxException(TRACE_INFO, "Unknown Value %s",
		stv.substr(pos).c_str());
}

/* ================================================================== */

/**
 * Decode a Valuation association list.
 * This list has the format
 * ((KEY . VALUE)(KEY2 . VALUE2)...)
 * Store the results as values on the atom.
 */
void DHTAtomStorage::decodeAlist(Handle& atom, std::string& alist)
{
	// Skip over opening paren
	size_t pos = 1;
	size_t totlen = alist.size();
	pos = alist.find('(', pos);
	while (std::string::npos != pos and pos < totlen)
	{
		++pos;  // over first paren of pair
		Handle key(decodeStrAtom(alist, pos));
		pos = alist.find(" . ", pos);
		pos += 3;
		ValuePtr val(decodeStrValue(alist, pos));
		atom->setValue(key, val);
		pos = alist.find('(', pos);
	}
}

/* ============================= END OF FILE ================= */
