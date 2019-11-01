/*
 * FILE:
 * opencog/persist/dht/DHTAtomStorage.h

 * FUNCTION:
 * OpenDHT-backed persistent storage.
 *
 * HISTORY:
 * Copyright (c) 2008,2009,2013,2017,2019 Linas Vepstas <linasvepstas@gmail.com>
 *
 * LICENSE:
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef _OPENCOG_DHT_ATOM_STORAGE_H
#define _OPENCOG_DHT_ATOM_STORAGE_H

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>
#include <vector>

#include <opendht.h>

#include <opencog/atoms/base/Atom.h>
#include <opencog/atoms/base/Link.h>
#include <opencog/atoms/base/Node.h>
#include <opencog/atoms/atom_types/types.h>
#include <opencog/atoms/value/FloatValue.h>
#include <opencog/atoms/value/LinkValue.h>
#include <opencog/atoms/value/StringValue.h>
#include <opencog/atoms/base/Valuation.h>

#include <opencog/atomspace/AtomTable.h>
#include <opencog/atomspace/BackingStore.h>

namespace opencog
{
/** \addtogroup grp_persist
 *  @{
 */

class DHTAtomStorage : public BackingStore
{
	private:
		void init(const char *);
		std::string _uri;
		int _port;
		std::string _atomspace_name;

		Handle tvpred; // the key to a very special valuation.

		dht::DhtRunner _runner;
		dht::InfoHash _atomspace_hash;

		// Storage policies
		dht::ValueType _atom_policy;
		dht::ValueType _space_policy;
		dht::ValueType _values_policy;
		dht::ValueType _incoming_policy;
		enum
		{
			ATOM_ID = 4097,
			SPACE_ID = 4098,
			VALUES_ID = 4099,
			INCOMING_ID = 4100,
		};

		// --------------------------
		// Fetch and storing of atoms

		std::string encodeValueToStr(const ValuePtr&);
		std::string encodeAtomToStr(const Handle& h) {
			return h->to_short_string(); }
		Handle decodeStrAtom(std::string&, size_t&);

		std::mutex _guid_mutex;
		std::unordered_map<Handle, dht::InfoHash> _guid_map;
		dht::InfoHash get_guid(const Handle&);

		Handle fetch_atom(const dht::InfoHash&);
		std::mutex _decode_mutex;
		std::map<dht::InfoHash, Handle> _decode_map;

		std::mutex _membership_mutex;
		std::unordered_map<Handle, dht::InfoHash> _membership_map;
		dht::InfoHash get_membership(const Handle&);

		std::mutex _publish_mutex;
		std::unordered_set<Handle> _published;
		void publish_to_atomspace(const Handle&);
		void store_recursive(const Handle&);

		// --------------------------
		// Bulk load and store
		bool bulk_load;
		bool bulk_store;
		time_t bulk_start;

		// --------------------------
		// Values
		void store_atom_values(const Handle &);
		Handle fetch_values(Handle&&);

		ValuePtr decodeStrValue(std::string&, size_t&);
		void decodeAlist(Handle&, std::string&);

		// --------------------------
		// Incoming set management
		void store_incoming_of(const Handle &, const Handle&);
		void remove_incoming_of(const Handle &, const std::string&);

		// --------------------------
		// Performance statistics
		std::atomic<size_t> _num_get_atoms;
		std::atomic<size_t> _num_got_nodes;
		std::atomic<size_t> _num_got_links;
		std::atomic<size_t> _num_get_insets;
		std::atomic<size_t> _num_get_inlinks;
		std::atomic<size_t> _num_node_inserts;
		std::atomic<size_t> _num_link_inserts;
		std::atomic<size_t> _num_atom_removes;
		std::atomic<size_t> _num_atom_deletes;
		std::atomic<size_t> _load_count;
		std::atomic<size_t> _store_count;
		std::atomic<size_t> _value_stores;
		time_t _stats_time;

	public:
		DHTAtomStorage(std::string uri);
		DHTAtomStorage(const DHTAtomStorage&) = delete; // disable copying
		DHTAtomStorage& operator=(const DHTAtomStorage&) = delete; // disable assignment
		virtual ~DHTAtomStorage();
		void bootstrap(const std::string& uri);
		bool connected(void); // connection to DB is alive

		std::string dht_examine(const std::string&);
		void load_atomspace(AtomSpace*, const std::string&);

		void kill_data(void); // destroy DB contents

		void registerWith(AtomSpace*);
		void unregisterWith(AtomSpace*);
		void extract_callback(const AtomPtr&);
		int _extract_sig;

		// AtomStorage interface
		Handle getNode(Type, const char *);
		Handle getLink(Type, const HandleSeq&);
		void getIncomingSet(AtomTable&, const Handle&);
		void getIncomingByType(AtomTable&, const Handle&, Type t);
		void storeAtom(const Handle&, bool synchronous = false);
		void removeAtom(const Handle&, bool recursive);
		void loadType(AtomTable&, Type);
		void loadAtomSpace(AtomTable&); // Load entire contents
		void storeAtomSpace(const AtomTable&); // Store entire contents
		void barrier();
		void flushStoreQueue();

		// Debugging and performance monitoring
		void print_stats(void);
		void clear_stats(void); // reset stats counters.
};


/** @}*/
} // namespace opencog

#endif // _OPENCOG_DHT_ATOM_STORAGE_H
