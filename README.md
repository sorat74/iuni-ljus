# Iuni-Ljus - In Ram Folded Tree Database with Default Journaling

Executable is pre-compiled for Debian 12.
To compile the src code (which is header only for simplicity) use g++ or use the prepared bash script.

IUNI-LJUS v0.11.0
Options:
  - version          : show version and exit
  - cli              : run cli
  - cli \<cmd\>        : run one command in the cli, in the default db
  - cli @\<db\> \<cmd\>  : run one command in the cli, in the <dbname> db
  - start            : start server
  - \[no params\]      : start server
  - local            : start server and run cli in the same process
  - help             : this help
  
Notice the software is in alfa version.

## How does it works

- Iuni stores in ram data to ensure fast data reads and writes.
- To ensure integrity, by DEFAULT, it journal each data updates.
- Iuni allows to store data in a tree structure, without the limitations of key-value dbs, and overcoming the rigidity of SQL databases.
- Iuni supports multiple databases (each has its own journal).
- Iuni introduces the concept of super normalization where if a token stored is inserted many times, it's actually reused without duplicating the same value in memory (many nodes points to the same value); this always since there are not tables.
- Fast access is reached through maps and advanced iteration processes.
- a cli is integrated in the software
- The server communicates with CLI and client via a TCP server
- a full-ish JS SDK is provided (a half written C++ one too)

## To start
echo "Launching the server"
nohup ./iuni-ljus &
echo "Using the cli"
./iuni-ljus cli
SET products pencil price 1.50
SET products rubber price 0.90
SET products pen price 3.00
GET products


## Cli help

Cli options
 * CD     : change path of action
 * help   : this help
 * UP     : go to upper node
 * RO     : go to root
 * SET <...nodes>        : set nodes *
 * GET <...nodes>        : get nodes within specified path *
 * LS <...nodes> : same as GET <...nodes>
 * IS     : check if path node exist *
 * count <...nodes>      : count number of occurrencies
 * DEL    : delete leaf node of the specified path *
 * UPDATE <...path> <old node> <new node> : updates (if exists) the old node in the path with the new node *
 * DROP   : drop db *
 * TREE  : show tree within the specified path *
 * TRE    : same as TREE
 * TREEN : show tree within the specified path with nodes' ids
 * TREN  : same as TREEN
 * test   : test server connection
 * COMPACT        : compact database journal

  \* Available in the SDKs too

## LICENSE: BSD-3clause
