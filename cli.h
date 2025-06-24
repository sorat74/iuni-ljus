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

#include <iostream>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <termios.h>
#include <unistd.h>
#include <signal.h>

class Cli {
private:
	string title = "";
	string prompt = "";
	int history_cursor = 0;
	int nlines = 0;
	string input = "";
	int pos = 0;
	unsigned int cli_runnning = 1;
    vector<string> history;
    
	static pair<string,int> lastprompt;

	static void sigint_handler(int sig) {
	    if (lastprompt.second == 0)
			cout << "\nPress Ctrl + C again to exit or write 'exit'.";
	    cout << endl << lastprompt.first;
	    cout.flush();
	    if (lastprompt.second == 0)
	    	signal(SIGINT, NULL);
	    else
	    	lastprompt.second = 0;
	}
	static vector<char> meta_escapable;
public:
	void setMetaEscapable(vector<char> me) {
		meta_escapable = me;
	}
	
	class Cmd {
	public:
		vector<string> tokens;
		
		Cmd() {}
		Cmd(string content) {
			load(content);
		}
		
		void load (string content) {
			tokens.clear();
			string t = "";
			char ESCAPE = '\\';
			bool escaped = false;
			for (auto i : content) {
				if (i == ESCAPE and !escaped) {
					escaped = true;
					continue;
				}
				
				if (i == ' ' or i == '\t' or i == '\n') { 
					if (!escaped) {
						if (t.empty()) goto NEXT;
						tokens.push_back(t);
						t = "";
					}
					else t += i;
				}
				else if (i == '\\') {
					if (escaped) t += "\\";
				}
				else if (meta_escapable.size() > 0 and string(&meta_escapable[0], &meta_escapable[0] + meta_escapable.size()).find(i) != string::npos) {
					
					if (escaped) 
						if (t.empty()) t += "\\" + string(1,i);
						else t += string(1,i);
					else {
						tokens.push_back(string(1,i));
					}
				}
				else { // if the escaped char is not canonical preserve the \ as normal char (TODO best choice?),  wtf I wrote?!?
					if (escaped) t += "\\" + string(1,i);
					else t += i; // normal char add
				}

			NEXT:
				escaped = false;
			}
			if (!t.empty())
				tokens.push_back(t);
		}
		
		string get(int p) {
			if (p >= tokens.size()) return "";
			return tokens[p];
		}
		
		void print() {
			cout << "#" << tokens.size() << ": ";
			for (auto& i : tokens) cout << "[" << i << "]";
			cout << endl;
		}
	};
	
	Cli() {}
	
	Cli(string title) {
		init(title);	
	}

	void init (string title) {
		this->title = title;
	}
	
	void setPrompt(string prompt) {
		this->prompt = prompt;	
	}
		
	/*  return 1 -> again cli; 
		return 0 -> stop cli */
	void start(function<int(Cli::Cmd&)> fun, function<string()> prompting) {
		
		// Function to get the current terminal settings
		auto get_terminal_settings = []() -> struct termios {
		    struct termios term;
		    tcgetattr(STDIN_FILENO, &term);
		    return term;
		};
		
		// Function to set the terminal settings
		auto set_terminal_settings = [](const struct termios& term) -> void {
		    tcsetattr(STDIN_FILENO, TCSANOW, &term);
		};		
		
		// Save the current terminal settings
	    struct termios original_settings = get_terminal_settings();
	
		// Set the terminal to raw mode
		auto set_raw_terminal = [&original_settings, &set_terminal_settings]() -> void {
		    struct termios raw_settings = original_settings;
		    raw_settings.c_lflag &= ~(ICANON | ECHO);
		    set_terminal_settings(raw_settings);
		};
		
		auto set_original_terminal = [&set_terminal_settings, &original_settings](){
			set_terminal_settings(original_settings);
		};
			
		cout << this->title << " cli" << endl;
	
	    do {
	    	input = "";
	    	nlines = 0;
	    	pos = 0;
	    	
			set_raw_terminal();
//
			cout << prompting() << (nlines == 0 ? "> " : "- ");
	        cout.flush();
	        char c;
	NEW_CHAR:
			lastprompt = make_pair(prompting() + "> ", input.empty() ? 0 : 1);
			signal(SIGINT, this->sigint_handler);
			
//	        // Read a character from the user
			cout.flush();
	        read(STDIN_FILENO, &c, 1);
	
			if ((int)c == 91) {
				
			}	
			else if ((int)c >= 32 and (int)c <= 126) { // normal char
				cout << c;
				cout.flush();
				
				if (pos >= input.length()) input.resize(pos+1, ' ');
				input[pos] = c; 
				
				pos++;
				history_cursor = history.size();
			}
			else if (c == 27) {
				char c[128];
				int bytes_read = read(STDIN_FILENO, c, 128);
//			//	for (int i=0; i<bytes_read; i++)
//			//		cout << "(" << (int)c[i] << ")";
				if (c[0] == 91 and c[1] == 65) { // up
					if (history_cursor != 0) {
						history_cursor--;
						string preinput = input;
						input = history[history_cursor];
						cout << "\r" << prompting() << "> " << string(preinput.size(), ' ')
							 << "\r" << prompting() << "> " << input;
						pos = input.size();
						cout.flush();
					}
				}
				else if (c[0] == 91 and c[1] == 66) { // down
					if (history_cursor + 1 < history.size()) {
						history_cursor++;
						input = history[history_cursor];
						cout << "\r" << prompting() << "> " << input;
					}
					else {
						cout << "\r" << prompting() << "> " << string(input.length(), ' ')
							 << "\r" << prompting() << "> ";
						input = "";
						pos = 0;
						nlines = 0;
					}
				}
				else if (c[0] == 91 and c[1] == 67) { // right
					if (pos == input.length()) goto NEW_CHAR;
					cout << input[pos];
					cout.flush();
					pos++;
				}
				else if (c[0] == 91 and c[1] == 68) { // left
					if (input.empty() or pos == 0) goto NEW_CHAR;
					pos--;
					cout << "\b";
				}
			}
			else if (c == 127) { // backspace
				if (input.empty()) goto NEW_CHAR;
				if (pos == 0) goto NEW_CHAR;

				int presize = input.size();
				if (pos == input.size()) input.pop_back();
				else input.erase(pos-1, 1);

				cout << "\r" << prompting() << "> ";
				for (int j=0; j<presize; j++) cout << ' ';
				for (int j=0; j<presize; j++) cout << '\b';
				cout << input;
				pos--;
				for (int j=0; j<input.size() - pos; j++) cout << '\b';
				cout.flush();
			}
			else if (c == 10) { // enter = aka \n
				cout << endl;
				cout.flush();
				
				if (input.back() == '\\') {
					nlines++;
					input += "\n";
					pos++;
					goto NEW_CHAR;
				}
				else if (input == "exit") {
	            	cli_runnning = false;
	            	continue;
	        	}
				
				history.push_back(input);
				history_cursor = history.size();

				Cmd cmd(input);
	cmd.print();
				if (cmd.tokens.empty()) continue;
				
				cli_runnning = fun(cmd);
				prompting();
				
				cout.flush();
				continue;
			}
			else {
				cout << "(" << (int)c << ")";
			}
			
			cout.flush();
			goto NEW_CHAR;
	
		} while (cli_runnning == 1);
	
	    // Restore the original terminal settings
		set_terminal_settings(original_settings);
	}
	
	void start(function<int(Cli::Cmd&)> fun) {
		this->start(fun, []() -> string { return ""; });
	}
};

vector<char> Cli::meta_escapable{};
pair<string,int> Cli::lastprompt = make_pair("", 0);

