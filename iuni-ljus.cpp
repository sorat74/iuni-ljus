
/**********************************************************
* Copyright 2025 Andrea Sorato.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
*
* 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS” AND ANY EXPRESS OR IMPLIED WARRANTIES, 
* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, 
* OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This file is part of iuni-ljus. Official website: iuni-ljus.org . 
***********************************************************/

/* creator of the iuni-ljus software: andrea sorato */
/* iuni-ljus : in ram super-normalized tree database with default journaling */
#define VERSION "0.11.0"

#include <iostream>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <map>
#include <mutex>
#include <iomanip>
#include <thread>
#include <fstream>
#include <atomic>
#include <tuple>
#include <filesystem>
using namespace std;
#include "utils.h"
#include "tcp.h"
#include "cli.h"
#include "spinlock.h"

class Bean;
map<string, Bean>::iterator HEND;
class Iter {
public:
	Iter() {}
	Iter(long id) : id(id) {}
	long id;
	map<string, Bean>::iterator	last = HEND; // last son
	map<string, Bean>::iterator	prev = HEND; // previous brother
	map<string, Bean>::iterator	next = HEND; // next brother
};

class Bean {
public:
	map<long, Iter> son_of;
	void print(ostream& strm) {
		for (auto& i : son_of) {
			strm << "    " << i.first << ": " << i.second.id << " ";
			strm << "{ "
				<< "last: " << (i.second.last == HEND ? "HEND" : i.second.last->first)
				<< ", prev: " << (i.second.prev == HEND ? "HEND" : i.second.prev->first)
				<< ", next: " << (i.second.next == HEND ? "HEND" : i.second.next->first) <<	" }";
			strm << endl;
		}
	}
	
	void print() {
		print(cout);
	}
	
	long getOneID(vector<long> exclusions) { /* if in a future version 'virtual beans' will be implemented, this function will have to be replaced */
		if (son_of.empty()) {
			cerr << "Fatal error " << __LINE__ << endl;
			exit(0);
		}
		
		for (auto i : son_of) {
			if (son_of.size() == 1) return i.second.id;
			
			if (Utils::contains(exclusions, i.second.id)) continue;	
			return i.second.id;
		}
		cerr << "Fatal error " << __LINE__ << endl;
		exit(0);
		return -1;
	}
	
	long getOneID() {
		return getOneID({});
	}
};


class Database {
private:
	friend int append (string, string, Database*);
	const string JRNL_EXTENSION = ".txt";
	const string JRNL_BASENAME = "./jrnl_";

	bool isCanonicalName (string);
	void reg (bool, vector<string>);
	
	string jrnl = JRNL_BASENAME + JRNL_EXTENSION;
	string database_name;
	
	recursive_mutex mtx_heap;
	
	Utils::UUIDgenerator uuid{};
	
	void checkUUID();
	void setConnections();

	ofstream* jfile = NULL;
	void close();
	
	vector<map<string, Bean>::iterator> getSons_ (Iter);
	map<string,bool> getSons (Iter);
	int waterfall_delete (Iter&, int&);
	Iter root{0}; // or Iter root = Iter(0);
	
	void get__ (vector<string>, vector<Iter>&, Iter, int, bool);
	void set__ (vector<string>, int, Iter*, int&, bool);
	void del__ (vector<string>, int, Iter*, int&, bool, vector<string> toupdate);
	
	void printSons (Iter, ostream&);
public:
	tuple<int,int,int> load();
	
	// db data methods
	pair<vector<string>, int> get_ (vector<string>);
	int is_ (vector<string>);
	int set_ (vector<string>);
	int del_ (vector<string>);
//	int put_ (vector<string>);
	string tree_ (vector<string>, string);
	int count_ (vector<string>);
	int drop_();
	int upd_(vector<string>, vector<string>);
	
	void printHeap(ostream& strm);

	void lock() { mtx_heap.lock(); }
	void unlock() { mtx_heap.unlock(); }
	
	int compact();
	
	Database() {}
	void setName (string database_name) {
		this->database_name = database_name;
		this->jrnl = JRNL_BASENAME + database_name + JRNL_EXTENSION;
	}
	
	string getJournalName() {
		return jrnl;	
	}

private:
	map<string, Bean> heap;
};

class DatabasePool {
private:
	mutex mtx;
	map<string, Database> dbpool;

	bool isCanonicalName (string name) {
		for (char c : name) {
	        if (!std::isalnum(c) && c != '_') {
			    return false;
	        }
	    }
		return true;
	}
public:
	const int USE_EXIT__FIRST = 0;
	const int USE_EXIT__AVAILABLE = 1;
	const int USE_EXIT__ILLEGAL_NAME = -1;
	const int USE_EXIT__OTHER_ERROR =  -2;
	
	pair<Database*, int> use (string database_name) { // database x USE_EXIT__<CODE>
		if (!isCanonicalName(database_name)) {
	        cout << "Not allowed: database names can contain only alphanumerical chars and underscores." << endl;
	        return make_pair(nullptr, USE_EXIT__ILLEGAL_NAME);
	    }
		
		lock_guard<mutex> lg(mtx);
		
		bool first = false;
		if (dbpool.find(database_name) == dbpool.end())
			first = true;
			
		Database& db = dbpool[database_name];
		if (first) { /* first time loading this db */
			db.setName(database_name);
			tuple<int,int,int> load_exit = db.load();
			if (load_exit == make_tuple<int, int,int>(-1, -1, -1)) {
				return make_pair(nullptr, USE_EXIT__OTHER_ERROR);	
			}
			cout << "Loaded '" << database_name; // db.getJournalName();
			if (load_exit != make_tuple<int, int, int>(0,0,0))
				cout << "' with " << get<1>(load_exit) << " nodes and " << get<2>(load_exit) << " connections." << endl;
			else cout << "'" << endl;
		}
		
		return make_pair(&db, first ? USE_EXIT__FIRST : USE_EXIT__AVAILABLE);
	}
	
	vector<string> getDatabaseList() {
		vector<string> dbl;
		lock_guard<mutex> lg(mtx);
		for (auto& i : dbpool)
			dbl.push_back(i.first);
		return dbl;
	}
} DBpool;

void Database::checkUUID() {
	for (auto& i : this->heap) {
		for (auto& e : i.second.son_of) {
			if (e.first > uuid.see()) this->uuid.set(e.first);
			if (e.second.id > uuid.see()) this->uuid.set(e.second.id);
		}
	}
}

void Database::close() {
	if (this->jfile != NULL)
		this->jfile->close();
}

void Database::setConnections() {
	map<long, vector<map<string, Bean>::iterator>> m; // parent x sons
	map<long, int> cover;
	
	for (auto it = heap.begin(); it != heap.end(); it++) {
		for (auto& j : it->second.son_of) {
			m[j.first].push_back(it);
		}
	}
	
	for (auto& i : this->heap) {
		for (auto& j : i.second.son_of) {
			
			Iter& iter = j.second;
			
			// A
			auto ff = m.at(j.first);
			
			int index = cover[j.first];
			if (cover.at(j.first) >= 1) {
				iter.prev = m.at(j.first)[index-1];
			}
			if (cover.at(j.first) + 1 < ff.size()) {
				iter.next = m.at(j.first)[index+1];
			}
			cover.at(j.first)++;
			
			// B
			auto f = m.find(iter.id);
			if (f != m.end()) iter.last = f->second.back(); // otherwise no last, aka last = HEND
		}
	}
	
	if (!m.empty())
		this->root.last = m.at(0).back();
}
	
void Database::printHeap(ostream& strm) {
	lock_guard<recursive_mutex> lg(mtx_heap);
	strm << "*** Heap ***\n";
	for (auto& i : this->heap) {
		strm << i.first << endl;
		i.second.print();
	}
}


string webSerialize (string s) { // for pseudo-JSON
	const bool quote = false;
	
	string z = "";
	for (auto i : s){
		if (i == '\n') {
			z += "\\n";
			continue;
		}

		if (i == '\\') z += '\\';
		else if (quote and i == '"') z += '\\';

		z += i;
	}
	return quote ? ('"' + z + '"') : z;
}

string webDeserialize(string s) {
    string result = "";
    bool inEscape = false;
    
    // Trim the leading and trailing double quotes
    if (s.length() >= 2 && s[0] == '"' && s[s.length() - 1] == '"') {
        s = s.substr(1, s.length() - 2);
    }
    
    for (char c : s) {
        if (inEscape) {
            if (c == 'n') {
                result += '\n';
            } else {
                result += c;
            }
            inEscape = false;
        } else if (c == '\\') {
            inEscape = true;
        } else {
            result += c;
        }
    }
    
    return result;
}

void webDeserializeBox (string s, string& z) {
	vector<string> v;
	Utils::splitbychar(s, '\n', v);
	int level = -1;
	z = "";
	int count = 0;

	for (auto& i : v) {
		if (i == "{") level++;
		else if (i == "}") level--;
		else {
			for (int l=0; l<level; l++) z += "   ";
//			z += webDeserialize(i);
			z += i;
			if (count + 1 != v.size()) z += "\n";
		}
		
		count++;
	}
}

string encodeForIuniTcpProtocol (string s) { /* serialize command for iuni-ljus tcp protocol */
	string z = "";
	for (auto i : s){
		if (i == '\n') {
			z += "\\n";
			continue;
		}

		if (i == '\\') z += '\\';
		else if (i == '|') z += '\\';

		z += i;
	}
	return z;
}


inline Iter& getAssociatedIter (map<string, Bean>::iterator row, long parent_id) {
	return row->second.son_of.at(parent_id);
}


void Database::printSons (Iter iter, ostream& strm) {
	vector<map<string, Bean>::iterator> v = getSons_(iter);
	if (!v.empty()) {
		strm <<  "{" << endl;
	}
	for (auto& i : v) {
		strm << webSerialize(i->first) << endl; // probably not pure to webSerialize here, instead do it in the TCP deliver TODO
		Iter& g = getAssociatedIter(i, iter.id);
		printSons(g, strm);
	}
	if (!v.empty()) strm << "}" << endl;
	strm.flush();
}


int append (string filename, string content, Database* db) {
	if (db->jfile == NULL) {
		db->jfile = new ofstream(filename, ios::app);
	    if (!db->jfile->is_open()) {
	        cerr << "Unable to open the file." << endl;
	        return 1;
	    }
	}
	
	// TODO check if journal still exists and it's writable

    (*db->jfile) << content;
	(*db->jfile).flush();
    return 0;
}



int touch (string filename, Database* db) {
	return append(filename, "", db);
}

SpinLock sp;
bool do_not_journal = false;
void Database::reg (bool condition, vector<string> v) {
	if (do_not_journal) return;
	if (!condition) return;
	
	string s = "";
	for (int i=0; i<v.size(); i++) /* no need to encode here: each slug in CLI or WEB request is aleady encoded */
		s += encodeForIuniTcpProtocol(v[i]) + (i+1 == v.size() ? "" : "|");
	sp.lock();
	append(jrnl, s + "\n", this);
	sp.unlock();
}

void parseJournalLine (string content, vector<string>& tokens, const char* splitter) {
	tokens.clear();
	string t = "";
	char ESCAPE = '\\';
	bool escaped = false;
	
	for (auto i : content) {
		if (i == ESCAPE and !escaped) {
			escaped = true;
			continue;
		}

		if (!escaped) {
			if (splitter != NULL and i == *splitter and !t.empty()) {
				tokens.push_back(t);
				t = "";
			}
			else t += i;
		}
		else {
			if (i == 'n') t += '\n';
			else if (i == ESCAPE) t += ESCAPE;
			else if (i == '|') t += '|';

			else t += "\\" + string(1,i);
		}
	NEXT:
		escaped = false;
	}
	if (!t.empty())
		tokens.push_back(t);
}

const string OP__INSERT = "i";
const string OP__MATRIX = "m";
const string OP__REFERENCE = "h";
const string OP__DROPDB = "p";
const string OP__DEL_MA = "d";
const string OP__DEL_NO = "e";
const string LOG__LOAD  = "l";

void 
Database::set__ (vector<string> keys, int spanner, Iter* prev_iter, int& amt, bool nowildcard) {
	for (int i=spanner; i<keys.size(); i++) {
		auto& k = keys[i];
		
		if (k == "*" and !nowildcard) {
			map<string,bool> uncles = getSons(*prev_iter);
			for (auto& u : uncles) {
				k = u.first;
				set__(keys, i, prev_iter, amt, true);
			}
			return;
		}
		if (!nowildcard) k = Utils::replaceAll(k, "\\*", "*");
		nowildcard = false;

//		cout << ">> " << k << endl;
		auto ins = heap.insert({k, Bean()});
		
		/* */ reg(ins.second, {OP__INSERT, k});
		long u = uuid.get();
		auto ins2 = ins.first->second.son_of.insert({prev_iter->id, Iter(u)});
		if (ins2.second) amt++;

		if (ins2.second) {
			ins2.first->second.prev = prev_iter->last; // the older brother of this son is the last son before this one
			if (prev_iter->last != HEND) 
				getAssociatedIter(prev_iter->last, prev_iter->id).next = ins.first; // the last son (if exists) has this one as the smaller brother
			prev_iter->last = ins.first; // this is the new son
		}

		/* */ long one_id = ins.first->second.getOneID({u});
		/* */ reg(!ins.second and ins2.second, {OP__REFERENCE, to_string(one_id)});
		/* */ reg(ins2.second, {OP__MATRIX, to_string(prev_iter->id), to_string(u)});
		prev_iter = &ins2.first->second;
	}
}


int
Database::set_ (vector<string> keys) {
	int amt = 0;
	set__(keys, 0, &root, amt, false);
	return amt;
}


int Database::waterfall_delete (Iter& parent_iter, int& amt) { // from = last 'last'
	vector<map<string, Bean>::iterator> sons = getSons_(parent_iter);
	
	// recursive delete
	for (auto& i : sons) {
//		cout << "* " << i->first << endl;
		Iter& grandson = getAssociatedIter(i, parent_iter.id);
		waterfall_delete(grandson, amt);
		
		i->second.son_of.erase(parent_iter.id);
		/* */ reg(true, {OP__DEL_MA, to_string(grandson.id), to_string(parent_iter.id)});
		
		if (i->second.son_of.empty()) {
			heap.erase(i->first); 
			/* */ reg(true, {OP__DEL_NO, to_string(grandson.id)});
		}
	}
	
	return amt;	
}

void
Database::del__ (vector<string> keys, int spanner, Iter* prev_iter, int& amt, bool nowildcard, vector<string> toupdate) {
	for (int i=spanner; i<keys.size(); i++) {
		auto& k = keys[i];

		if (k == "*" and !nowildcard) {
			map<string, bool> uncles = getSons(*prev_iter);
			for (auto& u : uncles) {
				k = u.first;
				del__(keys, i, prev_iter, amt, true, toupdate);
			}
			return;
		}
		if (!nowildcard) Utils::replaceAll(k, "\\*", "*");
		nowildcard = false;

		auto f = heap.find(k);
		if (f == heap.end()) return;

		if (i+1 == keys.size()) {
			/* */ long bean_id = f->second.getOneID();
			auto target = f->second.son_of.find(prev_iter->id);

			if (target != f->second.son_of.end()) { // if node to delete exists
				waterfall_delete(target->second, amt);
				
				if (prev_iter->last == f)
					prev_iter->last = target->second.prev; //'.next' - bug solved ??
				if (target->second.prev != HEND)
					getAssociatedIter(target->second.prev, prev_iter->id).next = target->second.next;
				if (target->second.next != HEND)
					getAssociatedIter(target->second.next, prev_iter->id).prev = target->second.prev;

				f->second.son_of.erase(prev_iter->id); /* the node is no more child of prev_iter */
				amt++;
				/* */ reg(true, {OP__DEL_MA, to_string(bean_id), to_string(prev_iter->id)});
				
				if (!toupdate.empty() and !keys.empty()) { // snippet for the upd_
					vector<string> keys2(keys.begin(), keys.end()-1);
					keys2.insert(keys2.end(), toupdate.begin(), toupdate.end());
					set_(keys2);
				}
			}
			
			bool empty_node = f->second.son_of.empty();
			/* */ reg(empty_node, {OP__DEL_NO, to_string(bean_id)});
			if (empty_node) heap.erase(f);
			return;
		}

		prev_iter = &f->second.son_of.at(prev_iter->id);
	}
}

int 
Database::del_ (vector<string> keys) {
	int amt = 0;
	del__(keys, 0, &root, amt, false, {});
	return amt;
}

int 
Database::drop_() {
	heap.clear();
	root.last = HEND;
	reg(true, {OP__DROPDB});
	return 0;
}	


vector<map<string, Bean>::iterator>
Database::getSons_ (Iter parent_iter) {
	vector<map<string, Bean>::iterator> results;

	map<string, Bean>::iterator	it = parent_iter.last;
	while (it != HEND) {
		results.push_back(it);
		it = it->second.son_of.at(parent_iter.id).prev;
	}

	return results;
}

map<string, bool>
Database::getSons (Iter parent_iter) {
	vector<map<string, Bean>::iterator> results = getSons_(parent_iter);
	map<string,bool> sresults;
	for (auto& i : results)
		sresults[i->first];
	return sresults;
}

Iter aborted1(-1);
Iter aborted2(-2);

void
Database::get__ (vector<string> keys, vector<Iter>& prev_ids_results, Iter prev_iter, int spanner, bool nowildcard) {
	for (int i=spanner; i<keys.size(); i++) {
		auto& k = keys[i];
		
		if (k == "*" and !nowildcard) {
			map<string,bool> uncles = getSons(prev_iter);
			for (auto& u : uncles) {
				k = u.first;
				get__(keys, prev_ids_results, prev_iter, i, true);
			}
			return;
		}
		if (!nowildcard) k = Utils::replaceAll(k, "\\*", "*");
		nowildcard = false;

		auto f = heap.find(k);

		if (f == heap.end()) {
			prev_iter = aborted1; // aborted
			break;
		}
		
		auto fs = f->second.son_of.find(prev_iter.id);
		if (fs == f->second.son_of.end()) {
			prev_iter = aborted2; // aborted
			break;
		}
		
		prev_iter = fs->second;
	}
		
	prev_ids_results.push_back(prev_iter);
}

pair<vector<string>, int> 
Database::get_ (vector<string> keys) {
	vector<Iter> prev_ids_results;
	get__(keys, prev_ids_results, root, 0, false);
	
	map<string, bool> mresults;
	int n_aborted = 0; // 0 -> no elements below, -1 -> aborted
	for (auto& i : prev_ids_results) {
		if (i.id < 0) {n_aborted++; continue;}
		map<string,bool> els = getSons(i);
		// results.insert(results.end(), els.begin(), els.end());
		mresults.insert(els.begin(), els.end());
	}

	vector<string> results;
	for (auto& i : mresults) results.push_back(i.first); // use map as result and put in utils getKeys of map TODO

	// for (auto& r : results) r = deencode(r);

	return make_pair(results, 
		prev_ids_results.size() == 1 && n_aborted > 0 ? -1 : results.size()
	); // TODO flag
}

int 
Database::is_ (vector<string> keys) {
	vector<Iter> prev_ids_results;
	get__(keys, prev_ids_results, root, 0, false);
	for (auto& i : prev_ids_results)
		if (i.id < 0) return 0;
	return 1;
}

int
Database::count_ (vector<string> keys) {
	return get_(keys).second;
}

//int // TODO
//Database::put_ (vector<string> keys) { // TODO check procedure correctness !
//	int amt = 0;
//
//	vector<string> keys_except_last = keys;
//	if (!keys_except_last.empty()) keys_except_last.pop_back();
//	
//	auto e = get_(keys_except_last);
//	if (e.first.size() != e.second) {
//		return amt;
//	}
//
//	return set_(keys);
//}

int
Database::upd_ (vector<string> keys, vector<string> new_nodes) {
	int amt = 0;
	del__(keys, 0, &root, amt, false, new_nodes);
	return 0; // TODO return the right amt
}

//int
//Database::ups_ (vector<string> keys, vector<string> new_nodes) { //upsert TODO
//	
//}

string 
Database::tree_ (vector<string> keys, string indexer) {
	stringstream ss("");
	vector<Iter> nodes_to_scout;
	get__(keys, nodes_to_scout, root, 0, false);
	for (auto& nts : nodes_to_scout) {
		printSons(nts, ss);
	}
	if (ss.str() == "") ss << "<empty>";
	string s = ss.str();
	if (!s.empty() and s.back() == '\n') s.pop_back();
	return s;
}

tuple<int,int,int> Database::load() {
	cout << "Loading data (";
	int file_ok = touch(jrnl, this); /* create journal file if not existing */
	if (file_ok != 0) {
		return {-1, -1, -1};	
	}
	// with volatile load but do not touch if not exist TODO
	ifstream Ifile (jrnl);
	string line;
	
	// check Ifile readability TODO
	
	
	ifstream filedim(jrnl, std::ios::binary | std::ios::ate);
    streampos size = 0;
	if (filedim.is_open()) {
        size = filedim.tellg();
        int mb = (int)(size/8/1024/1024);
        cout << "DB size of " << (mb == 0 ? "< 1" : ("~ "+to_string(mb))) << " MB) ";
    } else {
        cerr << "Unable to open file" << endl;
    }
	
	map<string, Bean>::iterator last_bar;
	map<long, map<string, Bean>::iterator> index_cache;
	mutex mtx;
	int loaded = 0;
	
	auto printIndexCache = [&index_cache](){
		for (auto& j : index_cache) cout << j.first << " ";
		cout << endl;
	};
	
	const char* splitter = new char('|');

	bool loading = true;
	auto check = [&mtx, &loading]() -> bool {
		lock_guard<mutex> lg(mtx);
		return loading == true;	
	};
	thread ldngthread([&check](){ while(check()) {
		cout << "."; 
		std::this_thread::sleep_for(std::chrono::seconds(1));
		cout.flush();
	} });

	while (getline(Ifile, line)) {
		vector<string> slugs;
		if (!line.empty() and line[0] == '#') continue;
		parseJournalLine(line, slugs, splitter);
		if (slugs.empty()) {
			cerr << "Invalid load record" << endl;
			continue;
		}
		
//		cout << "Load read: " << line << endl;
		
		string t_op = slugs[0];
		if (t_op == OP__INSERT) {
//			cout << "Action 1\n";
			if (slugs.size() <= 1) {
				cerr << "Invalid record for OP " << OP__INSERT << endl;
				continue;
			}
			string& t_bar = slugs[1];

			last_bar = heap.insert({t_bar, Bean()}).first;
		}
		else if (t_op == OP__REFERENCE) {
//			cout << "Action 2\n";
			if (slugs.size() <= 1) {
				cerr << "Invalid record for OP " << OP__REFERENCE << endl;
				continue;
			}
			string t_index = slugs[1];
			last_bar = index_cache.at(stol(t_index));
		}
		else if (t_op == OP__MATRIX) {
//			cout << "Action 3\n";
			if (slugs.size() <= 2) {
				cerr << "Invalid record for OP " << OP__MATRIX << endl;
				continue;
			}
			long parent_id = stol(slugs[1]);
			long id = stol(slugs[2]);

			last_bar->second.son_of.insert({parent_id, Iter(id)}); 
			index_cache.insert({id, last_bar});
		}
		else if (t_op == OP__DEL_MA) {
//			cout << "Action 4\n";
			if (slugs.size() <= 2) {
				cerr << "Invalid record for OP " << OP__DEL_MA << endl;
				continue;
			}
			long bean_id = stol(slugs[1]);
			long id = stol(slugs[2]);

			auto it = index_cache.at(bean_id);
			int amt = it->second.son_of.erase(id);
			if (amt <= 0) {
				cerr << "Fatal error " << __LINE__ << endl;
				exit(0);
			}
		}
		else if (t_op == OP__DEL_NO) {
//			cout << "Action 5\n";
			if (slugs.size() <= 1) {
				cerr << "Invalid record for OP " << OP__DEL_NO << endl;
				continue;
			}
			
			long bean_id = stol(slugs[1]);
			heap.erase(index_cache.at(bean_id));
			index_cache.erase(bean_id);
		}		
		else if (t_op == OP__DROPDB) {
//			cout << "Action 6\n";
			heap.clear();
		}
		else if (t_op == LOG__LOAD) {
			continue;	
		}
		
		lock_guard<mutex> lg(mtx);
		loaded++;
	}

	long conns = 0;
	for (auto& i : this->heap) 
		conns += i.second.son_of.size();

	checkUUID();
	setConnections();
	if (!do_not_journal) reg(true, {LOG__LOAD});
	
	mtx.lock();
	loading = false;
	mtx.unlock();
	cout << endl;
	if (ldngthread.joinable()) ldngthread.join();
	
	Ifile.close();
	return {loaded, this->heap.size(), conns};
}

const string TITLE = "IUNI-LJUS";
int PORT = 7212;

void cli_help() {
	cout << "Cli options\n"
			"  CD 	 : change path of action\n"
			"  help	 : this help\n"
			"  UP 	 : go to upper node\n"
			"  RO	 : go to root\n"
			"  SET <...nodes>	: set nodes *\n"
			"  GET <...nodes>	: get nodes within specified path *\n"
			"  LS <...nodes>	: same as GET <...nodes>\n"
			"  IS	 : check if path node exist *\n"
			"  count <...nodes>	: count number of occurrencies\n"
			"  DEL	 : delete leaf node of the specified path *\n"
//			"  PUT <...nodes>   : set nodes only if the penultimate node already exists\n"
			"  UPDATE <...path> <old node> <new node> : updates (if exists) the old node in the path with the new node *\n"
			"  DROP	 : drop db *\n"
			"  TREE  : show tree within the specified path *\n"
			"  TRE	 : same as TREE\n"
			"  TREEN : show tree within the specified path with nodes' ids\n"
			"  TREN  : same as TREEN\n"
			"  test	 : test server connection\n"
			"  COMPACT	 : compact database journal\n"
			"\n* Available in the SDKs too\n"
		<< endl;
	;
}

void run_cli (string connected_database, Cli::Cmd* direct_cmd) {
	auto markSize = [](string& s) -> void {
		s.insert(0, Utils::padLeft(to_string(s.size()), 8, '0') + "\n");
	};
	
	const string NOT_CONNECTED = "Not connected";
	
	string preprompt = connected_database;
	vector<string> path;
		
	auto asker = [&path, &preprompt, &NOT_CONNECTED, &connected_database, &markSize, &direct_cmd](Cli::Cmd& cmd) -> int {
		string tcp_serialized_cmd = "";
		
		if (cmd.get(0) == "exit") return 0;
		if (cmd.get(0) == "help") { cli_help(); return 1; }
		if (cmd.get(0) == "RO") {
			path.clear();
			return 1;
		}
		if (cmd.get(0) == "echo") {
			bool first = true;
			for (auto& c : cmd.tokens) {
				if (first) {first = false; continue;}
				cout << encodeForIuniTcpProtocol(c);
			}
			cout << endl;
			return 1;
		}
		
		tcp_serialized_cmd = "USE\n" + connected_database + "\n";
		
		if (cmd.get(0) == "DBLIST") {
			tcp_serialized_cmd += "DBLIST";
			goto ASK;
		}
		
		cmd.tokens.insert(cmd.tokens.begin() + 1, path.begin(), path.end());
		
		if (cmd.get(0) == "CD") {
			auto maybe_path = vector<string>(cmd.tokens.begin()+1, cmd.tokens.end());
			
			tcp_serialized_cmd += "IS\n";
			for (int i=0; i<maybe_path.size(); i++) {
				tcp_serialized_cmd += encodeForIuniTcpProtocol(maybe_path[i])
					+ (i+1 == maybe_path.size() ? "" : "\n");
			}

			string answer = "";
			markSize(tcp_serialized_cmd);
			int exitcode = TcpClient::send("127.0.0.1", PORT, tcp_serialized_cmd, answer);
			
			if (exitcode < 0) {
				preprompt = NOT_CONNECTED;
				return 1;
			}
			
			if (answer == "0") { // aka answer of the IS_ request == false
				cout << "Node does not exist\n";
				return 1;
			}
			
			path = maybe_path;
			return 1;
		}

		if (cmd.get(0) == "UP") {			
			vector<string> maybe_path;
			for (int i=0; i+1<path.size(); i++) maybe_path.push_back(path[i]);
			
//			for (auto j : maybe_path) cout << "{" << j << "}";
//			cout << endl;
			
			tcp_serialized_cmd += "IS\n";
			for (int i=0; i<maybe_path.size(); i++) {
				tcp_serialized_cmd += encodeForIuniTcpProtocol(maybe_path[i])
					+ (i+1 == maybe_path.size() ? "" : "\n");
			}

			string answer = "";
			markSize(tcp_serialized_cmd);
			int exitcode = TcpClient::send("127.0.0.1", PORT, tcp_serialized_cmd, answer);
			
			if (exitcode < 0) {
				preprompt = NOT_CONNECTED;
				return 1;
			}
			
			if (answer == "0") {
				cout << "Node does not exist\n";
				return 1;
			}
			
			path = maybe_path;
			return 1;
		}
		
		
		for (int i=0; i<cmd.tokens.size(); i++) {
			tcp_serialized_cmd 
				+= encodeForIuniTcpProtocol(cmd.tokens[i])
				+ (i+1 == cmd.tokens.size() ? "" : "\n");
		}
//			cmd.print();
ASK:
		string answer;
		markSize(tcp_serialized_cmd);
		int exitcode = TcpClient::send("127.0.0.1", PORT, tcp_serialized_cmd, answer);
		
		if (cmd.get(0) == "USE" and (answer == "0" or answer == "1")) {
			preprompt = cmd.get(1);
			connected_database = cmd.get(1);
		}
		else if (cmd.get(0).substr(0, 3) == "TRE" and exitcode >= 0) {
			if (answer == "<empty>") {
				cout << answer << endl;
				cout.flush();
				return 1;
			}
			string z = "";
			webDeserializeBox(answer, z);
			cout << z;
			cout.flush();
		}
		else {
			if (exitcode >= 0)
				cout << answer << endl;
		}
		
		return 1;
	};
	
	if (direct_cmd != nullptr) {
		asker(*direct_cmd);
		return;
	}
		
	Cli cli(TITLE);
	cli.start(asker, [&]() -> string { return preprompt + ": " + Utils::join(path, " "); } );
}

std::string createUniqueFile(const std::string& desiredFileName) {
    namespace fs = std::filesystem;
    std::string fileName = desiredFileName;

    int counter = 1;
    while (fs::exists(fileName)) {
        fileName = desiredFileName + "_" + std::to_string(counter++);
    }
    std::ofstream newFile(fileName);
    newFile.close();

    return fileName;
}

int Database::compact() {
	// lock shared resources with mutexes TODO
	
	// close jrnl
	cerr << "Functionality not implemented yet.\n";
	return -1;
	
	// STEP BY STEP:
	
	// close journal file and open a temporary one
	this->close();
	
	string tmp_journal = createUniqueFile(this->jrnl + "_tmp");
	this->jfile = new ofstream(tmp_journal, ios::app);
	
	struct Instruction {
		string action;
		string s1;
		string s2;	
		
		Instruction(vector<string> slugs) {
			action = slugs[0];
			if (slugs.size() < 2) return;
			s1 = slugs[1];
			if (slugs.size() < 3) return;
			s2 = slugs[2];
		}
		
		void print() {
			cout << "A: " << action << ", s1: " << s1 << ", s2: " << s2 << endl;	
		}
	};
	
	vector<Instruction> ci;
	
	auto check = [&ci](Instruction& i){
		// load each line journaling in a vector, deliting eventuals calcelations
		// compact core
	};
	
	// read each line of the journal, as in the load
	const char* splitter = new char('|');
	string line;
	ifstream Jf (jrnl);
	while (getline(Jf, line)) {
		vector<string> slugs;
		if (!line.empty() and line[0] == '#') continue;
		parseJournalLine(line, slugs, splitter);
		
		if (slugs.empty()) {
			cerr << "Invalid load record" << endl;
			continue;
		}
		
		Instruction i(slugs);
		i.print();
		
		check(i);
	}
	
	Jf.close();
	this->close();
	
	// rename the journal as old, and flush the compacted one into the same name file
	// flush the temporary journal's instructions into the new compacted journal
	
	// continue now reusing the normal journaling
	this->jfile = new ofstream(jrnl, ios::app);
	
	return 0;
}

void doWork(string req, TcpServer::Response res, bool local) {
	if (!local) cout << "[[Received qry:]]\n" << req << endl;
	
	vector<string> lines;
	Utils::getLines(req, lines);
	bool ok = true;
	string emitting = "-1";
	if (lines.size() < 2) { // std command: USE <dbnam> COMMAND arg0 arg1 arg2 ...
		res.send(emitting);
		return;	
	}
	emitting = "Unknown command.";
	
	for (auto& i : lines) {
		vector<string> t;
		parseJournalLine(i,t,NULL);
		if (t.size() > 1) {
			cerr << "Fatal error " << __LINE__ << endl;
			emitting = "-2";
			ok = false;
		}
		else i = t[0];
	}
	
	if (lines[0] != "USE") {
		emitting = "-1";
		ok = false;
	}
	auto useexit = DBpool.use(lines[1]);
	if (useexit.first == NULL) {
		emitting = "-1";
		ok = false;
	}
	
	lines.erase(lines.begin(), lines.begin()+2); // erase context tokens (USE <dbname>)
	Database& db = *(useexit.first);
	
	if (ok) {
//		cout << "Executing qry" << endl;
		db.lock(); // at the moment lock each query TODO improve granularity, but still ACIDity
		string action = lines[0];
		vector<string> pars = vector<string>(lines.begin()+1, lines.end());
		if (action == "GET" or action == "LS") {
			auto r = db.get_(pars);
			for (auto& i : get<0>(r))
				i = webSerialize(i);
			string sr = Utils::join(get<0>(r), "\n");
			emitting = sr == "" ? "<none>" : sr;
		}
		else if (action == "SET")
			emitting = to_string(db.set_(pars));
		else if (action == "IS")
			emitting = to_string(db.is_(pars));
		else if (action == "DEL")
			emitting = to_string(db.del_(pars));
//		else if (action == "PUT") 
//			emitting = to_string(db.put_(pars));
		else if (action == "UPD") {
			if (pars.size() < 3)
				emitting = "-1";					
			else 
			{
				vector<string> old_nodes;
				vector<string> new_nodes;
				bool old = true;
				for (int k=0; k<pars.size(); k++) {
					if (pars[k] == ":") { old = false; continue; }
					(old ? old_nodes : new_nodes).push_back(pars[k]);
					for (auto& k : old_nodes) if (k == "\\:") k = ":";
					for (auto& k : new_nodes) if (k == "\\:") k = ":";
				}
				if (old_nodes.empty() or new_nodes.empty()) emitting = "-1";
				else
				emitting = to_string(db.upd_(old_nodes, new_nodes));
			}
		}
		else if (action == "DROP") 
			emitting = to_string(db.drop_());
		else if (action == "TREE" or action == "TRE")
			emitting = db.tree_(pars, "");
		else if (action == "TREEN" or action == "TREN")
			emitting = db.tree_(pars, "i");
		else if (action == "COUNT")
			emitting = to_string(db.count_(pars));
		else if (action == "USE") {
			if (pars.size() != 1) emitting = "-1";
			else emitting = to_string(DBpool.use(pars[0]).second);
		}
		else if (action == "COMPACT") {
			emitting = to_string(db.compact());
		}
		else if (action == "DBLIST") {
			vector<string> dblist = DBpool.getDatabaseList();
			emitting = Utils::join(dblist, "\n");
		}
		else if (action == "test")
			emitting = "Hello Cranjis!";
		db.unlock();
	}
	res.send(emitting);
	if (!local) cout << "[[Emitted:]]\n" << emitting << endl;
	
//	this_thread::sleep_for(chrono::milliseconds(500)); // favor the immediate answer to be printed rather the resource clean up
	// now the resource cleanup can start...
}

int main (int nargs, char* sargs[]) {
	vector<string> args;
	for (int i=0; i<nargs; i++) 
		args.push_back(sargs[i]);
		
	if (args.size() == 2 and args[1] == "version") {
		cout << TITLE << endl << "v" << VERSION << endl;
		return 0;
	}
	
	const string DEFAULT_DATABASE_NAME = "default";
	if (args.size() >= 2 and args[1] == "cli") {
		Cli::Cmd cmd;
		string str_promtp = "";
		string database_name = DEFAULT_DATABASE_NAME;
		
		for (int i=2; i<args.size(); i++) {
			if (args[i][0] == '@') {
				if (args[i].size() == 1) {
					cerr << "Database name must be provided @<dbname>." << endl;
					return 1;
				}
				database_name = args[i].substr(1);
				continue;
			}
			str_promtp += args[i] + " ";
		}
		cmd.load(str_promtp);
		
		
		run_cli(database_name, &cmd);
		return 0;	
	}
	
	vector<string> booted;
	bool pendtcp = false;
	bool mono = false;
	for (auto it=args.begin(); it != args.end(); ) {
		if ((*it).size() > 0 and (*it)[0] == '@') {
			string booted_db = (*it).substr(1, (*it).size()-1);
			DBpool.use(booted_db);
			booted.push_back(booted_db);
			it = args.erase(it);
		}
		else if (*it == "--pend") {
			pendtcp = true;
			it = args.erase(it);
		}
		else if ((*it).substr(0, 6) == "--port") {
			it = args.erase(it);
			if (it == args.end()) break;
			
			string supposed_port = (*it);
			if (!Utils::isNaturalNumber(supposed_port)) {
				continue;
			}
			int the_port = stoi(supposed_port);
			cout << "* Enabled server with port " << the_port << endl;
			PORT = the_port;
			it = args.erase(it);
		}
		else if (*it == "--mono") {
			mono = true;
			it = args.erase(it);
			cout << "* Non-threaded option enabled\n";
		}
		else if (*it == "--volatile") {
			do_not_journal = true;
			it = args.erase(it);
			cout << "* Volatile (do not journal) option enabled\n";
		}
		else it++;		
	}
	
	if (args.size() == 1 
		or (args.size() == 2 and args[1] == "start")
		or (args.size() == 2 and args[1] == "local")
	) {
		if (args.size() == 1) args.push_back("");
		cout << TITLE << endl
			<< "Starting..." << endl;
			
		if (args[1] == "local")
			cout << "* Server in the same process of the CLI can slow down answers being printed." << endl;
		
		DBpool.use(!booted.empty() ? booted[0] : DEFAULT_DATABASE_NAME);
		
		TcpServer tcps(PORT, mono ? 1 : 0);
	
		tcps.pick([&args](string req, TcpServer::Response res) -> void {
			// manage requests in multi thread
			// request: 8 chars for size of the body, endl, slugs with endl as separators
			string str_nbytes = "", real_req = "";
			int nbytes = 0;
			
			while (req.size() > 0) { // to manage concatenated requests
				if (req.size() <= 8) break;
				str_nbytes = req.substr(0, 8);
				if (!Utils::isNaturalNumber(str_nbytes)) {
					res.send("-1");
					return;
				}
				
				nbytes = stoi(str_nbytes);
				
				if (req.size() < 9 + nbytes) break;
				real_req = req.substr(9, nbytes);
	
				doWork(real_req, res, args[1] == "local");
				
				// TODO check range before substr
				req = req.substr(8 + 1 + nbytes);
			}
		});
		
		atomic<int> server_ready(0);		
		thread thr([&tcps, &server_ready, &pendtcp](){
			tcps.start([&server_ready](){
				server_ready++;
			}, pendtcp);
		});
		
		long timer = 0;
		while (server_ready == 0) {
			if (timer % 10 == 0) cout << "Loading...\n";
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			timer++;
		}
		if (timer > 0 and server_ready > 0) cout << "Loading completed.\n";

		if (args[1] == "local") {
			run_cli(!booted.empty() ? booted[0] : DEFAULT_DATABASE_NAME, nullptr);
			exit(0);
		}
		
		thr.join();
		return 0;
	}
			
	if (args.size() == 2 and args[1] == "dev") {
	}
	
	cout << TITLE << endl << "v" << VERSION << endl;
	cout << "Options:\n"
			"  version	   : show version and exit\n"
			"  cli		   : run cli\n"
			"  cli <cmd> 	   : run one command in the cli, in the default db\n"
			"  cli @<db> <cmd>  : run one command in the cli, in the <dbname> db\n"
			"  start		   : start server\n"
			"  [no params]	   : start server\n"
			"  local		   : start server and run cli in the same process\n"
			"  help		   : this help\n"
	;

	return 0;	
}
