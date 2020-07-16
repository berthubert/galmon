#pragma once

void showVersion(const char *pname, const char *hash) {
  std::cout <<"galmon tools (" <<pname <<") " <<hash <<std::endl;
  std::cout <<"built date " <<__DATE__ <<std::endl;
  std::cout <<"(C) AHU Holding BV - bert@hubertnet.nl - https://berthub.eu/" <<std::endl;
  std::cout <<"https://galmon.eu/ - https://github.com/ahupowerdns/galmon" <<std::endl;
  std::cout <<"License GPLv3: GNU GPL version 3  https://gnu.org/licenses/gpl.html" <<std::endl;
}
