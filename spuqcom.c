/*
 * 		SpuQcom
 * 		Hardware:	Digital AD expansion board REV2.1 "Tarantula" / any embedded device is a goal
 * 		Author: 	Tom "SpuQyballz" Santens
 * 		Version:	13/12/2013	REV0.0
 * 		note:
 */
#include <string.h>

#define INBUFSIZE		50		// the input-buffer width
#define	 OUTBUFSIZE		50
#define INSTACKSIZE		5		// the size of the spuqformat input-stack
#define OUTSTACKSIZE	5		// size of the spuqformat output-stack

#define PACKETSTART		'<'		// start character of a spuqcom package
#define PACKETSTOP		'>'		// stop character of a spuqcom package
#define	 SECTORTAG		'#'		// sector tag
#define SECSPECTAG		'='		// sector specifier tag
#define VALUETAG		'*'
#define VALSPECTAG		'&'
#define ERRORTAG		'@'

#define SECTORSIZE		10
#define SECSPECSIZE		10
#define VALUESIZE		10
#define VALSPECSIZE		10

#define SERVICES		20

// The SpuQcom format!
typedef struct{
	char sector[SECTORSIZE];
	char sectorspecifier[SECSPECSIZE];
	char value[VALUESIZE];
	char valuespecifier[VALSPECSIZE];
} spuqformat;

typedef struct{
	spuqformat data;
	int content;
} bufferformat;

// function call mechanism format
typedef struct{
	char sector[SECTORSIZE];
	char secspec[SECSPECSIZE];
	int *function;
} handlerframe;


// Global Variables first input mechanism
volatile char _spuqcom_inputbuffer[INBUFSIZE];
volatile int _spuqcom_inputpointer;
volatile int _spuqcom_inputoverload;
// Global Variables second input mechanism
volatile bufferformat _spuqcom_inputstack[INSTACKSIZE];
volatile int _spuqcom_instackpushptr;						// pointer to the latest added input of the stack
volatile int _spuqcom_instackpopptr;						// pointer to the next-in-line input of the stack
volatile char _spuqcom_outputstack[OUTSTACKSIZE][OUTBUFSIZE];
volatile int _spuqcom_outstackpushptr;
volatile int _spuqcom_outstackpopptr;
// Global variables service mechanism
volatile int _spuqcom_servicepointer=0;
handlerframe _spuqcom_service[SERVICES];
// Global variables output mechanism
volatile int *_spuqcom_outchar;
volatile int *_spuqcom_outstring;
volatile int _spuqcom_outputchannelflag;

// function prototypes
int _spuqcom_init(void);
int _spuqcom_input(char inputChar);
int _spuqcom_msg_out(char *msg);
int _spuqcom_decoder(char *inputdata);
int _spuqcom_push_input(spuqformat input);
int _spuqcom_pop_input(spuqformat *input);
int _spuqcom_handler(void);
int _spuqcom_addService(char *sector, char *sectorspecifier, int(*ptr2func)(char*, char*));

int _spuqcom_encoder_error(spuqformat output);
int _spuqcom_encoder_answer(spuqformat output);
int _spuqcom_get_output(char *output);
int _spuqcom_write_output(char *output);

// Global variables for the handler
//handlerframe services[]= { {.identifier="BLUB", .subidentifier="BLA", .function = &_spuqcom_msg_out()} };

/*
 * 	SpuQcom Initialization sequence
 * 		- pointer to function to send output strings to host device
 */
int _spuqcom_init(void){
	int error=0;
	int i;

	// clear inputbuffer
		_spuqcom_inputbuffer[0]='\0';
		_spuqcom_inputpointer=0;
	// initialize inputmechanism
		_spuqcom_inputoverload=0;
	// initialize inputstack
		_spuqcom_instackpushptr=0;
		_spuqcom_instackpopptr=0;
		// set all content-flags of the inputstack to 0;
		for(i=0;i<INSTACKSIZE;i++){
			_spuqcom_inputstack[i].content=0;
		}
	// initialize outputstack
		_spuqcom_outstackpushptr=0;
		_spuqcom_outstackpopptr=0;
		// set all content-flags of the inputstack to 0;
		for(i=0;i<OUTSTACKSIZE;i++){
			strcpy(_spuqcom_outputstack[i],"\0");
		}
	// initialize services
		_spuqcom_servicepointer=0;
		 _spuqcom_msg_out("SpuQcom Ready");

	return error;
}

/*
 * 	SpuQcom input mechanism
 * 	- Assemble full strings from input characters
 * 	- Set flag as a full packet is received
 */
int _spuqcom_input(char inputChar){
	int error=0;

	// first, check for the package start character
	if(inputChar==PACKETSTART){
		// reset the pointer of the inputbuffer to 0
		_spuqcom_inputpointer=0;
	}
	else{
		// increase the pointer which keeps track of the last input
		//_spuqcom_inputpointer++;
	}

	// only write a character to the buffer if there is enough space for the char itself and the '\0' thing
	if(_spuqcom_inputpointer<INBUFSIZE-1){
		// write the char in the buffer to the position of the pointer
		_spuqcom_inputbuffer[_spuqcom_inputpointer]=inputChar;
		// add the '\0' char to the end, for the string-handling stuff
		_spuqcom_inputbuffer[_spuqcom_inputpointer+1]='\0';
		// increase the pointer which keeps track of the last input
		_spuqcom_inputpointer++;

		// if the input character is the packet-stop, flush it to the decoder
		if(inputChar==PACKETSTOP){
			//_spuqcom_msg_out("\n\rDEBUG\tBefore entering decoder (%s)\n\r", _spuqcom_inputbuffer);
			error=_spuqcom_decoder(_spuqcom_inputbuffer);
			//_spuqcom_msg_out("DEBUG\tAfter decoder\n\r");
		}
	}
	else{
		// inform the host device there was a buffer overflow here
		_spuqcom_msg_out("Inputbuffer overflow\0");
		error=-1;
	}

	return error;
}


/*
 * 	SpuQcom decoder
 * 		- extracts the useful data from the input and puts it formatted on the inputstack
 */
int _spuqcom_decoder(char *inputdata){
	int error=0;
	char *stringpointer;
	char temp[100];
	int i;

	spuqformat outputdata;

	//strcpy(temp, "bla\0");

	// scan input for the different tags, the stringlibrary-way
	// SECTOR
	sprintf(temp, "%c",SECTORTAG);
	stringpointer = strstr(inputdata,temp);
	if(stringpointer!=0){
		i=0;
		do{
			i++;
			outputdata.sector[i-1]=stringpointer[i];
		}
		while( stringpointer[i+1]!= SECTORTAG && i<=SECTORSIZE-2);
		outputdata.sector[i]='\0';
		//_spuqcom_msg_out("DEBUG\tsector: %s\n\r", outputdata.sector);
	}

	// SECSPEC
	sprintf(temp, "%c",SECSPECTAG);
	stringpointer = strstr(inputdata,temp);
	if(stringpointer!=0){
		i=0;
		do{
			i++;
			outputdata.sectorspecifier[i-1]=stringpointer[i];
		}
		while( stringpointer[i+1]!= SECSPECTAG && i<=SECSPECSIZE-2);
		outputdata.sectorspecifier[i]='\0';
		//_spuqcom_msg_out("DEBUG\tsecspec: %s\n\r", outputdata.sectorspecifier);
	}
	else{
		strcpy(outputdata.sectorspecifier, "\0");
	}
	// value
	sprintf(temp, "%c",VALUETAG);
	stringpointer = strstr(inputdata,temp);
	if(stringpointer!=0){
		i=0;
		do{
			i++;
			outputdata.value[i-1]=stringpointer[i];
		}
		while( stringpointer[i+1]!= VALUETAG && i<=VALUESIZE-2);
		outputdata.value[i]='\0';
		//_spuqcom_msg_out("DEBUG\tvalue: %s\n\r", outputdata.value);
	}
	// value specifier
	sprintf(temp, "%c",VALSPECTAG);
	stringpointer = strstr(inputdata,temp);
	if(stringpointer!=0){
		i=0;
		do{
			i++;
			outputdata.valuespecifier[i-1]=stringpointer[i];
		}
		while( stringpointer[i+1]!= VALSPECTAG && i<=VALSPECSIZE-2);
		outputdata.valuespecifier[i]='\0';
		//_spuqcom_msg_out("DEBUG\tvalspec: %s\n\r", outputdata.valuespecifier);
	}

	// put fresh input on top of the inputstack
	if(_spuqcom_push_input(outputdata)<0){
		error=-1;
	}

	return error;
}

/*
 * SpuQcom input push
 * 		- push data onto the input stack and updates the input push stackpointer
 */

int _spuqcom_push_input(spuqformat input){
	int error = 0;
	int i;

	// browse the stack for a new empty space
	for(i=0;i<INSTACKSIZE;i++){
		if(_spuqcom_inputstack[_spuqcom_instackpushptr].content !=0){
			// if we do find an empty spot, step out of the loop and continue
			break;
		}
		// increase the pointer
		_spuqcom_instackpushptr++;
		// circular mechanism for the stack
		if(_spuqcom_instackpushptr>=INSTACKSIZE){
			// if the instackpushptr points higher than the stack is large, make it 0 again
			_spuqcom_instackpushptr=0;
		}
	}

	if( (_spuqcom_inputstack[_spuqcom_instackpushptr].content == 0) ){
		// the empty spot on the stack gets filled with the new input
		_spuqcom_inputstack[_spuqcom_instackpushptr].data = input;
		// set the content-indicator on a value higher than 0
		_spuqcom_inputstack[_spuqcom_instackpushptr].content = 0xFF;
	}
	else{
		// if we did not find an empty spot on the stack, let the host device know the stack is full
		_spuqcom_msg_out("Inputstack is full\0");
		error = -1;
	}


	return error;
}
/*
 * 	SpuQcom input pop
 * 		- pops data from the bottom of the input stack
 */
int _spuqcom_pop_input(spuqformat *input){
	int error = 0;
	int i;
	// find data on the stack by browsing it maximum on time
	for(i=0;i<INSTACKSIZE;i++){
		if(_spuqcom_inputstack[_spuqcom_instackpopptr].content>0){
			// if we've found content, stop browsing
			break;
		}
		// increase the pop-pointer with one
		_spuqcom_instackpopptr++;
		// circular mechanism
		if(_spuqcom_instackpopptr>=INSTACKSIZE){
			// if the instackpopptr points higher than the stack is large, make it 0 again
			_spuqcom_instackpopptr=0;
		}
	}

	if(_spuqcom_inputstack[_spuqcom_instackpopptr].content>0){
		// when we've found something on the stack, copy it to the argument's variable
		*input = _spuqcom_inputstack[_spuqcom_instackpopptr].data;
		// set the content to 0
		_spuqcom_inputstack[_spuqcom_instackpopptr].content=0;
	}
	else{
		// buffer is empty, no new data
		error = -1;
	}

	return error;
}
/*
 * 		========
 * 		SERVICES
 * 		========
 */

int _spuqcom_handler(void){
	spuqformat input;
	int (*ptr2func)(char*,char*);
	int i;
	int foundflag=0;

	// first try to retrieve data from the inputstack
	if(_spuqcom_pop_input(&input)<0){
		// failed to retrieve data from stack
		return -1;
	}

	// if we have data, start the lookup for the according function
	for(i=0;i<SERVICES;i++){
		if(strcmp(input.sector,_spuqcom_service[i].sector)==0 && strcmp(input.sectorspecifier, _spuqcom_service[i].secspec)==0){
			// if we've found a match with the sector and the sector specifier, assign the function to execute and quit the loop
			ptr2func=_spuqcom_service[i].function;
			foundflag=1;
			break;
		}
	}

	if(foundflag<=0){
		// spuqcom error
		_spuqcom_msg_out("NOT AN INSTRUCTION");
		foundflag=0;
		return -1;
	}

	// execute the function
	if(ptr2func(input.value, input.valuespecifier)<0){
		// in case of an error, function which gives error-code to host (output mechanism)
		_spuqcom_encoder_error(input);

	}
	else{
		// in case all went fine, function which returns a solution to host (output mechanism)
		_spuqcom_encoder_answer(input);
	}

	return 0;
}
/*
 * 		====================
 * 		PROGRAMMER INTERFACE
 * 		====================
 */
int _spuqcom_addService(char *sector, char *sectorspecifier, int(*ptr2func)(char*, char*)){
	// no error detection because strcpy has no error mechanism... = rely on the programmer
	if(_spuqcom_servicepointer>=SERVICES){
		// if there is no more room to add a service, return error
		return -1;
	}

	strcpy(_spuqcom_service[_spuqcom_servicepointer].sector, sector);
	strcpy(_spuqcom_service[_spuqcom_servicepointer].secspec , sectorspecifier);
	_spuqcom_service[_spuqcom_servicepointer].function = *ptr2func;
	_spuqcom_servicepointer++;

	return 0;
}

int _spuqcom_removeService(char *sector, char *sectorspecifier){

	return 0;
}

int _spuqcom_char_output(void(*ptr2func)(char)){
	int error=0;

	*_spuqcom_outchar = *ptr2func;
	_spuqcom_outputchannelflag=1;

	return error;
}

int _spuqcom_string_output(void(*ptr2func)(char *)){
	int error=0;

	*_spuqcom_outstring = *ptr2func;
	_spuqcom_outputchannelflag=2;

	return error;
}

/*
 * SpuQcom answer mechanism
 */
int _spuqcom_encoder_error(spuqformat output){
	int error=0;
	char outputstring[OUTBUFSIZE];
	int i;

	snprintf(outputstring, OUTBUFSIZE, "%c%c%s%c%c%s%c%c%s%c%c\0", PACKETSTART,
													SECTORTAG, output.sector, SECTORTAG,
													SECSPECTAG, output.sectorspecifier, SECSPECTAG,
													ERRORTAG, output.value, ERRORTAG,
													PACKETSTOP);

	error=_spuqcom_write_output(outputstring);
	return error;
}

int _spuqcom_encoder_answer(spuqformat output){
	int error=0;
	char outputstring[OUTBUFSIZE];
	int i;

	snprintf(outputstring, OUTBUFSIZE, "%c%c%s%c%c%s%c%c%s%c%c%s%c%c\0", PACKETSTART,
														SECTORTAG, output.sector, SECTORTAG,
														SECSPECTAG, output.sectorspecifier, SECSPECTAG,
														VALUETAG, output.value, VALUETAG,
														VALSPECTAG, output.valuespecifier, VALSPECTAG,
														PACKETSTOP);

	// write new data to the output buffer
	_spuqcom_write_output(outputstring);

	return error;
}
/*
 * 	SpuQcom error output mechanism
 * 	- Delivers error messages to the host device
 */
int _spuqcom_msg_out(char *msg){
	int error=0;
	char outputstring[OUTBUFSIZE];

	snprintf(outputstring, OUTBUFSIZE, "%c%c%s%c%c\0", PACKETSTART,
														ERRORTAG, msg, ERRORTAG,
														PACKETSTOP);

	// write new data to the output buffer
	_spuqcom_write_output(outputstring);

	return error;
}

int _spuqcom_get_output(char *output){
	//int error=0;
	int i;

	// scan buffer for an empty spot
	for(i=0;i<OUTSTACKSIZE;i++){
		if( _spuqcom_outputstack[_spuqcom_outstackpopptr][0] == PACKETSTART ){
			// if we've found something to say
			break;
		}
		_spuqcom_outstackpopptr++;

		if(_spuqcom_outstackpopptr>=OUTSTACKSIZE){
			_spuqcom_outstackpopptr=0;
		}
	}

	if( _spuqcom_outputstack[_spuqcom_outstackpopptr][0] == PACKETSTART ){
		strcpy(output, _spuqcom_outputstack[_spuqcom_outstackpopptr]);
		_spuqcom_outputstack[_spuqcom_outstackpopptr][0]='\0';
		return 0;
	}

	return -1;
}

int _spuqcom_write_output(char *output){
	int i;

	// scan buffer for an empty spot
	for(i=0;i<OUTSTACKSIZE;i++){
		if( strcmp(_spuqcom_outputstack[_spuqcom_outstackpushptr] , "\0") == 0 ){
			// if we've found something to say
			break;
		}
		_spuqcom_outstackpushptr++;

		if(_spuqcom_outstackpushptr>=OUTSTACKSIZE){
			_spuqcom_outstackpushptr=0;
		}
	}

	// write new data to the output buffer
	if(strcmp(_spuqcom_outputstack[_spuqcom_outstackpushptr] , "\0") == 0){
		strcpy(_spuqcom_outputstack[_spuqcom_outstackpushptr], output);
		return 0;
	}

	//_spuqcom_msg_out("DEBUG\t failed to write to ouputstack");
	return -1;
}
