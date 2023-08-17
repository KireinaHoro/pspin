%{
#include <hrtimer.h>

#include <mpi.h>
#include <stdio.h>
#include <assert.h>

#include <iostream>
#include <iomanip>
#include <vector>
#include <queue>
#include <list>
#include <algorithm>

#include "ddtparser.h"

using namespace std;

int calc_num(int start, int stride, int stop);

extern FILE * yyin;
extern "C" int yylex (void);
void yyerror(const char *);

typedef struct yy_buffer_state * YY_BUFFER_STATE;
extern "C" YY_BUFFER_STATE yy_scan_string(const char *str);
extern "C" void yy_delete_buffer(YY_BUFFER_STATE buffer);

unsigned long long g_timerfreq;

struct Datatype {
	MPI_Datatype    mpi;
};

struct Datatypes {
	list<struct Datatype> types;
};

struct Index {
	long int displ;
	int blocklen;
};

struct Indices {
	vector<struct Index *> indices;
};

struct Triple {
	int size;
	int subsize;
	int start;
};

struct Triples {
	vector<struct Triple *> triples;
};


struct Singles {
	vector<int> singles;
};

struct StructElem {
	MPI_Aint displ;
	int blocklen;
	struct Datatype datatype;
};

struct StructElems {
	vector<struct StructElem *> structelems;
};



vector<struct Datatype> datatypes;

%}

%union {
	long int val;
	struct Datatypes *types;

	struct Index *index;
	struct Indices *indices;

	struct {
		int start;
		int stop;
		int stride;
	} range;

	struct{
		int count;
		int blocklen;
	} pair;

	struct Triple *triple;
	struct Triples *triples;
	struct Singles *singles;

	struct StructElem *structelem;
	struct StructElems *structelems;

};

%token <val> NUM
%token <sym> UNKNOWN SUBTYPE ELEM
%token <sym> CONTIGUOUS VECTOR HVECTOR HINDEXED STRUCT INDEXEDBLOCK RESIZED SUBARRAY
%token <sym> BYTE_ CHAR_ INT_ DOUBLE_ FLOAT_ INT32_T_ INT64_T_ UNSIGNED_CHAR_ SHORT_ UNSIGNED_SHORT_ LONG_ UNSIGNED_LONG_ LONG_DOUBLE_ UNSIGNED_LONG_LONG_ UNSIGNED_ DATATYPE_NULL_

%type <types> datatype primitive derived contiguous vector hvector hindexed indexedblock resized struct subarray
%type <indices> idxentries
%type <index>   idxentry
%type <range>   range
%type <pair>   pair
%type <val>   val
%type <triple>   triple
%type <triples>   triples
%type <singles>   singles
%type <structelem>   structelem
%type <structelems>   structelems


%start input

%%

input:
| input topdatatype
;

topdatatype:
datatype {
	datatypes.insert( datatypes.end(), $1->types.begin(), $1->types.end() );
	delete $1;
}
;

datatype:
primitive |
derived
;

primitive:
BYTE_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_BYTE;
	$$->types.push_back(datatype);
}
| CHAR_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_CHAR;
	$$->types.push_back(datatype);
}
| INT_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_INT;
	$$->types.push_back(datatype);
}
| FLOAT_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_FLOAT;
	$$->types.push_back(datatype);
}
| DOUBLE_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_DOUBLE;
	$$->types.push_back(datatype);
}
| INT32_T_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_INT32_T;
	$$->types.push_back(datatype);
}
| INT64_T_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_INT64_T;
	$$->types.push_back(datatype);
}
| UNSIGNED_CHAR_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_UNSIGNED_CHAR;
	$$->types.push_back(datatype);
}
| SHORT_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_SHORT;
	$$->types.push_back(datatype);
}
| LONG_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_LONG;
	$$->types.push_back(datatype);
}
| UNSIGNED_LONG_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_UNSIGNED_LONG;
	$$->types.push_back(datatype);
}
| LONG_DOUBLE_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_LONG_DOUBLE;
	$$->types.push_back(datatype);
}
| UNSIGNED_LONG_LONG_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_UNSIGNED_LONG_LONG;
	$$->types.push_back(datatype);
}
| UNSIGNED_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_UNSIGNED;
	$$->types.push_back(datatype);
}
| DATATYPE_NULL_ {
	$$ = new Datatypes;
	struct Datatype datatype;
	datatype.mpi  = MPI_DATATYPE_NULL;
	$$->types.push_back(datatype);
}
;

derived:
contiguous
| vector
| hvector
| hindexed
| struct
| indexedblock
| resized
| subarray
;


val:
NUM{
	$$ = $1;
}
;

range:
NUM {
	$$.start = $1;
	$$.stride = 1;
	$$.stop = $1;
}
| NUM ':' NUM ':' NUM {
	$$.start = $1;
	$$.stride = $3;
	$$.stop = $5;
}
;


contiguous:
CONTIGUOUS '(' range ')' '[' datatype ']' {
	$$ = new Datatypes;

	list<struct Datatype> *subtypes = &($6->types);
	list<struct Datatype> *types = &($$->types);

	for(list<struct Datatype>::iterator subtype = subtypes->begin();
		subtype != subtypes->end(); subtype++) {
		for (int count=$3.start; count <= $3.stop; count += $3.stride) {
			Datatype type;
			MPI_Type_contiguous(count, subtype->mpi, &(type.mpi));
			types->push_back(type);
		}
	}

	delete $6;
}
;

vector:
VECTOR '(' range range range ')' '[' datatype ']' {
	$$ = new Datatypes;

	list<struct Datatype> *subtypes = &($8->types);
	list<struct Datatype> *types = &($$->types);

	for(list<struct Datatype>::iterator subtype = subtypes->begin();
		subtype != subtypes->end(); subtype++) {
		for (int count=$3.start; count <= $3.stop; count += $3.stride) {
			for (int blocklen=$4.start; blocklen <= $4.stop; blocklen += $4.stride) {
				for (int stride=$5.start; stride <= $5.stop; stride += $5.stride) {
					Datatype type;
					MPI_Type_vector(count, blocklen, stride, subtype->mpi, &(type.mpi));
					types->push_back(type);
				}
			}
		}
	}

	delete $8;
}
;

hvector:
HVECTOR '(' range range range ')' '[' datatype ']' {
	$$ = new Datatypes;

	list<struct Datatype> *subtypes = &($8->types);
	list<struct Datatype> *types = &($$->types);

	for(list<struct Datatype>::iterator subtype = subtypes->begin();
		subtype != subtypes->end(); subtype++) {
		for (int count=$3.start; count <= $3.stop; count += $3.stride) {
			for (int blocklen=$4.start; blocklen <= $4.stop; blocklen += $4.stride) {
				for (int stride=$5.start; stride <= $5.stop; stride += $5.stride) {
					Datatype type;
					MPI_Type_create_hvector(count, blocklen, stride, subtype->mpi, &(type.mpi));
					types->push_back(type);
				}
			}
		}
	}

	delete $8;
}
;

idxentry:
NUM ',' NUM {
	$$ = new Index;
	$$->displ = $1;
	$$->blocklen = $3;
}
;

idxentries:
/* empty rule */ {
	$$ = new Indices;
}
| idxentries idxentry {
	$$ = $1;
	$$->indices.push_back($2);
}
;



hindexed:
HINDEXED '(' idxentries ')' '[' datatype ']' {
	$$ = new Datatypes;

	list<struct Datatype> *subtypes = &($6->types);
	list<struct Datatype> *types = &($$->types);

	unsigned int num = $3->indices.size();
	long *displs = (long*)malloc(num * sizeof(long));
	int *blocklens = (int*)malloc(num * sizeof(int));

	for(int i=0; i<num; i++) {
		displs[i] = $3->indices[i]->displ;
		blocklens[i] = $3->indices[i]->blocklen;
		delete $3->indices[i];
	}
	delete $3;

	for(list<struct Datatype>::iterator subtype = subtypes->begin();
		subtype != subtypes->end(); subtype++) {
		Datatype type;
		MPI_Type_create_hindexed(num, blocklens, displs, subtype->mpi, &(type.mpi));
		types->push_back(type);
	}

	free(displs);
	free(blocklens);
	delete $6;
}
;

pair:
NUM ',' NUM  {
	$$.count = $1;
	$$.blocklen = $3;
}
;

singles:
/* empty rule */ {
	$$ = new Singles;
}
| singles NUM {
	$$ = $1;
	$$->singles.push_back($2);
}
;


indexedblock:
INDEXEDBLOCK '(' val  ':'  singles ')' '[' datatype ']' {
	$$ = new Datatypes;

	list<struct Datatype> *subtypes = &($8->types);
	list<struct Datatype> *types = &($$->types);

	unsigned int num = $5->singles.size();;
	int *displs = (int*)malloc(num * sizeof(int));
	int blocklen = $3;

	for(int i=0; i<num; i++) {
		displs[i] = $5->singles[i];
	}
	delete $5;

	for(list<struct Datatype>::iterator subtype = subtypes->begin();
		subtype != subtypes->end(); subtype++) {
		Datatype type;
		MPI_Type_create_indexed_block(num, blocklen, displs, subtype->mpi, &(type.mpi));
		types->push_back(type);
	}

	free(displs);
}
;

resized:
RESIZED '(' pair ')' '[' datatype ']'  {
 	$$ = new Datatypes;

	list<struct Datatype> *subtypes = &($6->types);
	list<struct Datatype> *types = &($$->types);

	int lowerbound = $3.count;
	int extent = $3.blocklen;

	for(list<struct Datatype>::iterator subtype = subtypes->begin();
		subtype != subtypes->end(); subtype++) {
		Datatype type;
		MPI_Type_create_resized(subtype->mpi, lowerbound, extent, &(type.mpi));
		types->push_back(type);
	}

}
;

structelem:
NUM ',' NUM ',' datatype {
	$$ = new StructElem;
	$$->displ = $1;
	$$->blocklen = $3;
	$$->datatype = ($5->types).front();
 	delete $5;
}
;

structelems:
/* empty rule */ {
	$$ = new StructElems;
}
| structelems structelem {
	$$ = $1;
	$$->structelems.push_back($2);
}
;

struct:
STRUCT '(' structelems  ')'  {
 	$$ = new Datatypes;

	list<struct Datatype> *types = &($$->types);

	unsigned int num = $3->structelems.size();;
	MPI_Aint *displs = (MPI_Aint*)malloc(num * sizeof(MPI_Aint));
	int *blocklens = (int*)malloc(num * sizeof(int));
	MPI_Datatype *array_of_types = (MPI_Datatype*)malloc(num * sizeof(MPI_Datatype));


	for(int i=0; i<num; i++) {
		displs[i] = $3->structelems[i]->displ;
		blocklens[i] = $3->structelems[i]->blocklen;
		array_of_types[i] = $3->structelems[i]->datatype.mpi;
		delete $3->structelems[i];
	}
	delete $3;

	Datatype type;
	MPI_Type_create_struct(num, blocklens, displs, array_of_types, &(type.mpi));

	types->push_back(type);

  	free(displs);
  	free(blocklens);
  	free(array_of_types);

}
;


triple:
NUM ',' NUM ',' NUM {
	$$ = new Triple;
	$$->start = $1;
	$$->subsize = $3;
	$$->size = $5;
}
;


triples:
/* empty rule */ {
	$$ = new Triples;
}
| triples triple {
	$$ = $1;
	$$->triples.push_back($2);
}
;

subarray:
SUBARRAY '(' triples ')' '[' datatype ']' {
	$$ = new Datatypes;

	int order = MPI_ORDER_C;

	list<struct Datatype> *subtypes = &($6->types);
	list<struct Datatype> *types = &($$->types);

	unsigned int ndims = $3->triples.size();
	int *sizes = (int*)malloc(ndims * sizeof(int));
	int *subsizes = (int*)malloc(ndims * sizeof(int));
	int *starts = (int*)malloc(ndims * sizeof(int));

	for(int i=0; i<ndims; i++) {
		sizes[i] = $3->triples[i]->size;
		subsizes[i] = $3->triples[i]->subsize;
		starts[i] = $3->triples[i]->start;
		delete $3->triples[i];
	}
	delete $3;


	for(list<struct Datatype>::iterator subtype = subtypes->begin();
		subtype != subtypes->end(); subtype++) {
		Datatype type;

		MPI_Type_create_subarray(ndims, sizes, subsizes, starts, order, subtype->mpi, &(type.mpi));
         	types->push_back(type);
	}

	free(sizes);
	free(subsizes);
	free(starts);


}
;






%%

void yyerror(const char *s) {
	fprintf (stderr, "Error: %s\n", s);
}

int calc_num(int start, int stride, int stop) {
	return 1+(stop-start)/stride;
}

void alloc_buffer(size_t size, void** buffer, int alignment) {
	if (alignment == 1) {
		*buffer = malloc(size);
	}
	else {
		int ret = posix_memalign(reinterpret_cast<void**>(buffer), 16, size);
		if(ret){
			printf("Error in posix_memalign\n");
		}
		assert(ret == 1);
	}
	assert(buffer != NULL);
}

void init_buffer(size_t size, void* buf, bool pattern) {
	if (pattern) {
		for (size_t i=0; i<size; i++) {
			((char*)buf)[i] = i+1;
		}
	}
	else {
		for (size_t i=0; i<size; i++) {
			((char*)buf)[i] = 0;
		}
	}
}


#define WARMUP  5
#define NUMRUNS 10
#define TIME_HOT(code, median)                         \
do {                                                   \
	HRT_TIMESTAMP_T start, stop;                       \
	std::vector<uint64_t> times (NUMRUNS, 0);          \
	for (unsigned int i=0; i<WARMUP; i++) {            \
		code;                                          \
	}                                                  \
	for (unsigned int i=0; i<NUMRUNS; i++) {           \
		HRT_GET_TIMESTAMP(start);                      \
		code;                                          \
		HRT_GET_TIMESTAMP(stop);                       \
		HRT_GET_ELAPSED_TICKS(start, stop, &times[i]); \
	}                                                  \
	std::sort(times.begin(), times.end());             \
	median = HRT_GET_USEC(times[NUMRUNS/2]);           \
} while(0)

MPI_Datatype ddtparser_string2datatype(const char * str){

    YY_BUFFER_STATE buff = yy_scan_string(str);
    assert(yyparse() == 0);
    MPI_Datatype t = datatypes[0].mpi;

    yy_delete_buffer(buff);

    return t;
}


/*
int main(int argc, char **argv) {
	int token;

	if (argc < 2) {
		fprintf(stderr, "%s <string>\n", argv[0]);
		exit(1);
	}

	MPI_Init(&argc, &argv);
	HRT_INIT(0, g_timerfreq);


    int size;
    MPI_Type_size(t, &size);

    printf("size: %i\n", size);

}

*/
