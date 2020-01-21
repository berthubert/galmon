#pragma once

#define _showVersion(program,g_gitHash) { \
  cout <<"galmon tools (" <<program <<") " <<g_gitHash <<endl; \
  cout <<"built date " <<__DATE__ <<endl; \
  cout <<"(C) AHU Holding BV - bert@hubertnet.nl - https://berthub.eu/" <<endl; \
  cout <<"https://galmon.eu/ - https://github.com/ahupowerdns/galmon" <<endl; \
  cout <<"License GPLv3: GNU GPL version 3  https://gnu.org/licenses/gpl.html" <<endl; }
