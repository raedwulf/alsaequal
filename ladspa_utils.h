/* utils.h

   Free software by Richard W.E. Furse. Do with as you will. No
   warranty. */

#ifndef LADSPA_SDK_LOAD_PLUGIN_LIB
#define LADSPA_SDK_LOAD_PLUGIN_LIB

#include "ladspa.h"

/* This function call takes a plugin library filename, searches for
   the library along the LADSPA_PATH, loads it with dlopen() and
   returns a plugin handle for use with findPluginDescriptor() or
   unloadLADSPAPluginLibrary(). Errors are handled by writing a
   message to stderr and calling exit(1). It is alright (although
   inefficient) to call this more than once for the same file. */
void * LADSPAload(const char * pcPluginFilename);

/* This function unloads a LADSPA plugin library. */
void LADSPAunload(void * pvLADSPAPluginLibrary);

/* This function locates a LADSPA plugin within a plugin library
   loaded with loadLADSPAPluginLibrary(). Errors are handled by
   writing a message to stderr and calling exit(1). Note that the
   plugin library filename is only included to help provide
   informative error messages. */
const LADSPA_Descriptor *
LADSPAfind(void * pvLADSPAPluginLibrary,
			   const char * pcPluginLibraryFilename,
			   const char * pcPluginLabel);

/* Find the default value for a port. Return 0 if a default is found
   and -1 if not. */
int LADSPADefault(const LADSPA_PortRangeHint * psPortRangeHint,
		     const unsigned long          lSampleRate,
		     LADSPA_Data                * pfResult);


/* MMAP to a controls file */
#define LADSPA_CNTRL_INPUT	0
#define LADSPA_CNTRL_OUTPUT	1
typedef struct LADSPA_Control_Data_ {
	int index;
	LADSPA_Data data[16];	/* Max number of channels, would be nicer if 
								this wasn't a fixed number */
	int type;
} LADSPA_Control_Data;
typedef struct LADSPA_Control_ {
	unsigned long long length;
	unsigned long long id;
	unsigned long long channels;
	unsigned long long num_controls;
	int input_index;
	int output_index;
	LADSPA_Control_Data control[];
} LADSPA_Control;
LADSPA_Control * LADSPAcontrolMMAP(const LADSPA_Descriptor *psDescriptor,
		const char *controls_filename, unsigned int channels);
void LADSPAcontrolUnMMAP(LADSPA_Control *control);

#endif
