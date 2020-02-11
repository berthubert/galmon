/*
            cerr<<"Preamble "<<getbitu(&cnav[0], 0, 8)<<" sv "<<
              getbitu(&cnav[0], 8, 6) << " type "<< type <<
              " tow " << tow;

            if(type == 10) {
              wn = getbitu(&cnav[0], 38, 13);
              cerr<<" t0p " << 300*getbitu(&cnav[0], 54,11)<< " t0e "<< 300*getbitu(&cnav[0], 70, 11);
            }
            else if(type == 11) {
              cerr<<" t0e "<< 300* getbitu(&cnav[0], 38, 11);

                            
            }
            else if(type == 32) {
              cerr<< " delta-ut1 " << 1000.0*ldexp(getbits(&cnav[0], 215, 31), -24)<< "ms rate " << 1000.0*ldexp(getbits(&cnav[0], 246, 19), -25) << " te0p " <<
                16*getbits(&cnav[0], 127, 16);
                
            }
*/
