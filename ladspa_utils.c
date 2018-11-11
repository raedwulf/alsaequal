/* ------------------------------------------------------------------
   Free software by Richard W.E. Furse. Do with as you will. No
   warranty.
  ------------------------------------------------------------------*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/mman.h>
#include <string.h>
#include <math.h>

#include "ladspa.h"
#include "ladspa_utils.h"

/* ------------------------------------------------------------------ */

/* This function provides a wrapping of dlopen(). When the filename is
   not an absolute path (i.e. does not begin with / character), this
   routine will search the LADSPA_PATH for the file. */
static void *
dlopenLADSPA(const char * pcFilename, int iFlag) {

  char * pcBuffer;
  const char * pcEnd;
  const char * pcLADSPAPath;
  const char * pcStart;
  int iEndsInSO;
  int iNeedSlash;
  size_t iFilenameLength;
  void * pvResult;

  iFilenameLength = strlen(pcFilename);
  pvResult = NULL;

  if (pcFilename[0] == '/') {

    /* The filename is absolute. Assume the user knows what he/she is
       doing and simply dlopen() it. */

    pvResult = dlopen(pcFilename, iFlag);
    if (pvResult != NULL)
      return pvResult;

  }
  else {

    /* If the filename is not absolute then we wish to check along the
       LADSPA_PATH path to see if we can find the file there. We do
       NOT call dlopen() directly as this would find plugins on the
       LD_LIBRARY_PATH, whereas the LADSPA_PATH is the correct place
       to search. */

    pcLADSPAPath = getenv("LADSPA_PATH");

    if (pcLADSPAPath) {

      pcStart = pcLADSPAPath;
      while (*pcStart != '\0') {
	pcEnd = pcStart;
	while (*pcEnd != ':' && *pcEnd != '\0')
	  pcEnd++;

	pcBuffer = malloc(iFilenameLength + 2 + (pcEnd - pcStart));
	if (pcEnd > pcStart)
	  strncpy(pcBuffer, pcStart, pcEnd - pcStart);
	iNeedSlash = 0;
	if (pcEnd > pcStart)
	  if (*(pcEnd - 1) != '/') {
	    iNeedSlash = 1;
	    pcBuffer[pcEnd - pcStart] = '/';
	  }
	strcpy(pcBuffer + iNeedSlash + (pcEnd - pcStart), pcFilename);

	pvResult = dlopen(pcBuffer, iFlag);

	free(pcBuffer);
	if (pvResult != NULL)
	  return pvResult;

	pcStart = pcEnd;
	if (*pcStart == ':')
	  pcStart++;
      }
    }
  }

  /* As a last ditch effort, check if filename does not end with
     ".so". In this case, add this suffix and recurse. */
  iEndsInSO = 0;
  if (iFilenameLength > 3)
    iEndsInSO = (strcmp(pcFilename + iFilenameLength - 3, ".so") == 0);
  if (!iEndsInSO) {
    pcBuffer = malloc(iFilenameLength + 4);
    strcpy(pcBuffer, pcFilename);
    strcat(pcBuffer, ".so");
    pvResult = dlopenLADSPA(pcBuffer, iFlag);
    free(pcBuffer);
  }

  if (pvResult != NULL)
    return pvResult;

  /* If nothing has worked, then at least we can make sure we set the
     correct error message - and this should correspond to a call to
     dlopen() with the actual filename requested. The dlopen() manual
     page does not specify whether the first or last error message
     will be kept when multiple calls are made to dlopen(). We've
     covered the former case - now we can handle the latter by calling
     dlopen() again here. */
  return dlopen(pcFilename, iFlag);
}

/* ------------------------------------------------------------------ */

void * LADSPAload(const char * pcPluginFilename) {

  void * pvPluginHandle;

  pvPluginHandle = dlopenLADSPA(pcPluginFilename, RTLD_NOW);
  if (!pvPluginHandle) {
    fprintf(stderr,
	    "Failed to load plugin \"%s\": %s\n",
	    pcPluginFilename,
	    dlerror());
    exit(1);
  }

  return pvPluginHandle;
}


void LADSPAunload(void * pvLADSPAPluginLibrary) {
  dlclose(pvLADSPAPluginLibrary);
}

const LADSPA_Descriptor * LADSPAfind(void * pvLADSPAPluginLibrary,
			   const char * pcPluginLibraryFilename,
			   const char * pcPluginLabel) {

  const LADSPA_Descriptor * psDescriptor;
  LADSPA_Descriptor_Function pfDescriptorFunction;
  unsigned long lPluginIndex;

  dlerror();
  pfDescriptorFunction
    = (LADSPA_Descriptor_Function)dlsym(pvLADSPAPluginLibrary,
					"ladspa_descriptor");
  if (!pfDescriptorFunction) {
    const char * pcError = dlerror();
    if (pcError) {
      fprintf(stderr,
	      "Unable to find ladspa_descriptor() function in plugin "
	      "library file \"%s\": %s.\n"
	      "Are you sure this is a LADSPA plugin file?\n",
	      pcPluginLibraryFilename,
	      pcError);
      exit(1);
    }
  }

  for (lPluginIndex = 0;; lPluginIndex++) {
    psDescriptor = pfDescriptorFunction(lPluginIndex);
    if (psDescriptor == NULL) {
      fprintf(stderr,
	      "Unable to find label \"%s\" in plugin library file \"%s\".\n",
	      pcPluginLabel,
	      pcPluginLibraryFilename);
      exit(1);
    }
    if (strcmp(psDescriptor->Label, pcPluginLabel) == 0)
      return psDescriptor;
  }
}

/* ------------------------------------------------------------------ */

int LADSPADefault(const LADSPA_PortRangeHint * psPortRangeHint,
		 const unsigned long          lSampleRate,
		 LADSPA_Data                * pfResult) {

  int iHintDescriptor;

  iHintDescriptor = psPortRangeHint->HintDescriptor & LADSPA_HINT_DEFAULT_MASK;

  switch (iHintDescriptor & LADSPA_HINT_DEFAULT_MASK) {
  case LADSPA_HINT_DEFAULT_NONE:
    return -1;
  case LADSPA_HINT_DEFAULT_MINIMUM:
    *pfResult = psPortRangeHint->LowerBound;
    if (LADSPA_IS_HINT_SAMPLE_RATE(psPortRangeHint->HintDescriptor))
      *pfResult *= lSampleRate;
    return 0;
  case LADSPA_HINT_DEFAULT_LOW:
    if (LADSPA_IS_HINT_LOGARITHMIC(iHintDescriptor)) {
      *pfResult = exp(log(psPortRangeHint->LowerBound) * 0.75
		      + log(psPortRangeHint->UpperBound) * 0.25);
    }
    else {
      *pfResult = (psPortRangeHint->LowerBound * 0.75
		   + psPortRangeHint->UpperBound * 0.25);
    }
    if (LADSPA_IS_HINT_SAMPLE_RATE(psPortRangeHint->HintDescriptor))
      *pfResult *= lSampleRate;
    return 0;
  case LADSPA_HINT_DEFAULT_MIDDLE:
    if (LADSPA_IS_HINT_LOGARITHMIC(iHintDescriptor)) {
      *pfResult = sqrt(psPortRangeHint->LowerBound
		       * psPortRangeHint->UpperBound);
    }
    else {
      *pfResult = 0.5 * (psPortRangeHint->LowerBound
			 + psPortRangeHint->UpperBound);
    }
    if (LADSPA_IS_HINT_SAMPLE_RATE(psPortRangeHint->HintDescriptor))
      *pfResult *= lSampleRate;
    return 0;
  case LADSPA_HINT_DEFAULT_HIGH:
    if (LADSPA_IS_HINT_LOGARITHMIC(iHintDescriptor)) {
      *pfResult = exp(log(psPortRangeHint->LowerBound) * 0.25
		      + log(psPortRangeHint->UpperBound) * 0.75);
    }
    else {
      *pfResult = (psPortRangeHint->LowerBound * 0.25
		   + psPortRangeHint->UpperBound * 0.75);
    }
    if (LADSPA_IS_HINT_SAMPLE_RATE(psPortRangeHint->HintDescriptor))
      *pfResult *= lSampleRate;
    return 0;
  case LADSPA_HINT_DEFAULT_MAXIMUM:
    *pfResult = psPortRangeHint->UpperBound;
    if (LADSPA_IS_HINT_SAMPLE_RATE(psPortRangeHint->HintDescriptor))
      *pfResult *= lSampleRate;
    return 0;
  case LADSPA_HINT_DEFAULT_0:
    *pfResult = 0;
    return 0;
  case LADSPA_HINT_DEFAULT_1:
    *pfResult = 1;
    return 0;
  case LADSPA_HINT_DEFAULT_100:
    *pfResult = 100;
    return 0;
  case LADSPA_HINT_DEFAULT_440:
    *pfResult = 440;
    return 0;
  }

  /* We don't recognise this default flag. It's probably from a more
     recent version of LADSPA. */
  return -1;
}

/* ------------------------------------------------------------------ */

void LADSPAcontrolUnMMAP(LADSPA_Control *control)
{
	munmap(control, control->length);
}

LADSPA_Control * LADSPAcontrolMMAP(const LADSPA_Descriptor *psDescriptor,
		const char *controls_filename, unsigned int channels)
{
	const char * homePath;
	char *filename;
	unsigned long i, j, num_controls, index;
	LADSPA_Control *default_controls;
	LADSPA_Control *ptr;
	int fd;
	unsigned long length;

	if(channels > 16) {
		fprintf(stderr, "Can only control a maximum of 16 channels.\n");
		return NULL;
	}

	/* Create config filename, if no path specified store in home directory */
	if (controls_filename[0] == '/') {
		filename = malloc(strlen(controls_filename) + 1);
		if (filename==NULL) {
			return NULL;
		}
		sprintf(filename, "%s", controls_filename);
	} else {
		homePath = getenv("HOME");
		if (homePath==NULL) {
			return NULL;
		}
		filename = malloc(strlen(controls_filename) + strlen(homePath) + 2);
		if (filename==NULL) {
			return NULL;
		}
		sprintf(filename, "%s/%s", homePath, controls_filename);
	}

	/* Count the number of controls */
	num_controls = 0;
	for(i = 0; i < psDescriptor->PortCount; i++) {
		if(psDescriptor->PortDescriptors[i]&LADSPA_PORT_CONTROL) {
			num_controls++;
		}
	}

	if(num_controls == 0) {
		fprintf(stderr, "No Controls on LADSPA Module.\n");
		return NULL;
	}

	/* Calculate the required file-size */
	length = sizeof(LADSPA_Control) +
			num_controls*sizeof(LADSPA_Control_Data) +
			num_controls*sizeof(LADSPA_Data)*channels;

	/* Open config file */
	fd = open(filename, O_RDWR);
	if(fd < 0) {
		if(errno == ENOENT){
			/* If the file doesn't exist create it and populate
				it with default data. */
			fd = open(filename, O_RDWR | O_CREAT, 0664);
			if(fd < 0) {
				fprintf(stderr, "Failed to open controls file:%s.\n",
						filename);
				free(filename);
				return NULL;
			}
			/* Create default controls stucture */
			default_controls = malloc(length);
			if(default_controls == NULL) {
				free(filename);
				return NULL;
			}
			default_controls->length = length;
			default_controls->id = psDescriptor->UniqueID;
			default_controls->channels = channels;
			default_controls->num_controls = num_controls;
			default_controls->input_index = -1;
			default_controls->output_index = -1;
			for(i = 0, index=0; i < psDescriptor->PortCount; i++) {
				if(psDescriptor->PortDescriptors[i]&LADSPA_PORT_CONTROL) {
						default_controls->control[index].index = i;
						LADSPADefault(&psDescriptor->PortRangeHints[i], 44100,
								&default_controls->control[index].data[0]);
					for(j = 1; j < channels; j++) {
						default_controls->control[index].data[j] =
								default_controls->control[index].data[0];
					}
					if(psDescriptor->PortDescriptors[i]&LADSPA_PORT_INPUT) {
						default_controls->control[index].type = LADSPA_CNTRL_INPUT;
					} else {
						default_controls->control[index].type = LADSPA_CNTRL_OUTPUT;
					}
					index++;
				} else if(psDescriptor->PortDescriptors[i] ==
						(LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO)) {
					default_controls->input_index = i;
				} else if(psDescriptor->PortDescriptors[i] ==
						(LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO)) {
					default_controls->output_index = i;
				}
			}
			if((default_controls->output_index == -1) ||
				(default_controls->input_index == -1)) {
					fprintf(stderr,
						"LADSPA Plugin must have one audio channel\n");
				free(default_controls);
				free(filename);
				return NULL;
			}
			/* Write the deafult data to the file. */
			if(write(fd, default_controls, length) < 0) {
				free(default_controls);
				free(filename);
				return NULL;
			}
			free(default_controls);
		} else {
			free(filename);
			return NULL;
		}
	}

	/* MMap Configuration File */
	ptr = (LADSPA_Control*)mmap(NULL, length,
			PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close (fd);

	if(ptr == MAP_FAILED) {
		free(filename);
		return NULL;
	}

	/* Make sure we're mapped to the right file type. */
	if(ptr->length != length) {
		fprintf(stderr, "%s is the wrong length.\n",
				filename);
		LADSPAcontrolUnMMAP(ptr);
		free(filename);
		return NULL;
	}

	if(ptr->id != psDescriptor->UniqueID) {
		fprintf(stderr, "%s is not a control file for ladspa id %" PRId32 ".\n",
				filename, ptr->id);
		LADSPAcontrolUnMMAP(ptr);
		free(filename);
		return NULL;
	}

	if(ptr->channels != channels) {
		fprintf(stderr, "%s is not a control file doesn't have %ud channels.\n",
				filename, channels);
		LADSPAcontrolUnMMAP(ptr);
		free(filename);
		return NULL;
	}

	free(filename);
	return ptr;
}
