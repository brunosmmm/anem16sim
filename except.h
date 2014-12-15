/*
 * except.h
 *
 *  Created on: 15/12/2014
 *      Author: bruno
 */

#ifndef EXCEPT_H_
#define EXCEPT_H_

#include <exception>

class ANEMProgramLoadException : public std::exception
{
	virtual const char * what(void) const throw()
		{
			return "Problem loading program file";
		}

};

extern ANEMProgramLoadException ANEM_PROGRAM_LOAD_EXCEPT;


#endif /* EXCEPT_H_ */
