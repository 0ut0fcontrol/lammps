/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

// C or Fortran style library interface to LAMMPS
// customize by adding new LAMMPS-specific functions

#include <mpi.h>
#include <string.h>
#include <stdlib.h>
#include "library.h"
#include "lmptype.h"
#include "lammps.h"
#include "universe.h"
#include "input.h"
#include "atom_vec.h"
#include "atom.h"
#include "domain.h"
#include "update.h"
#include "group.h"
#include "input.h"
#include "variable.h"
#include "modify.h"
#include "output.h"
#include "thermo.h"
#include "compute.h"
#include "fix.h"
#include "comm.h"
#include "memory.h"
#include "error.h"

using namespace LAMMPS_NS;

// ----------------------------------------------------------------------
// utility macros
// ----------------------------------------------------------------------

/* ----------------------------------------------------------------------
   macros for optional code path which captures all exceptions
   and stores the last error message. These assume there is a variable lmp
   which is a pointer to the current LAMMPS instance.

   Usage:

   BEGIN_CAPTURE
   {
     // code paths which might throw exception
     ...
   }
   END_CAPTURE
------------------------------------------------------------------------- */

#ifdef LAMMPS_EXCEPTIONS
#define BEGIN_CAPTURE \
  Error * error = lmp->error; \
  try

#define END_CAPTURE \
  catch(LAMMPSAbortException & ae) { \
    int nprocs = 0; \
    MPI_Comm_size(ae.universe, &nprocs ); \
    \
    if (nprocs > 1) { \
      error->set_last_error(ae.message.c_str(), ERROR_ABORT); \
    } else { \
      error->set_last_error(ae.message.c_str(), ERROR_NORMAL); \
    } \
  } catch(LAMMPSException & e) { \
    error->set_last_error(e.message.c_str(), ERROR_NORMAL); \
  }
#else
#define BEGIN_CAPTURE
#define END_CAPTURE
#endif

// ----------------------------------------------------------------------
// helper functions, not in library API
// ----------------------------------------------------------------------

/* ----------------------------------------------------------------------
   concatenate one or more LAMMPS input lines starting at ptr
   removes NULL terminator when last printable char of line = '&'
     by replacing both NULL and '&' with space character
   repeat as many times as needed
   on return, ptr now points to longer line
------------------------------------------------------------------------- */

void concatenate_lines(char *ptr)
{
  int nend = strlen(ptr);
  int n = nend-1;
  while (n && isspace(ptr[n])) n--;
  while (ptr[n] == '&') {
    ptr[nend] = ' ';
    ptr[n] = ' ';
    strtok(ptr,"\n");
    nend = strlen(ptr);
    n = nend-1;
    while (n && isspace(ptr[n])) n--;
  }
}

// ----------------------------------------------------------------------
// library API functions to create/destroy an instance of LAMMPS
//   and communicate commands to it
// ----------------------------------------------------------------------

/* ----------------------------------------------------------------------
   create an instance of LAMMPS and return pointer to it
   pass in command-line args and MPI communicator to run on
------------------------------------------------------------------------- */

void lammps_open(int argc, char **argv, MPI_Comm communicator, void **ptr)
{
#ifdef LAMMPS_EXCEPTIONS
  try
  {
    LAMMPS *lmp = new LAMMPS(argc,argv,communicator);
    *ptr = (void *) lmp;
  }
  catch(LAMMPSException & e) {
    fprintf(stderr, "LAMMPS Exception: %s", e.message.c_str());
    *ptr = (void *) NULL;
  }
#else
  LAMMPS *lmp = new LAMMPS(argc,argv,communicator);
  *ptr = (void *) lmp;
#endif
}

/* ----------------------------------------------------------------------
   create an instance of LAMMPS and return pointer to it
   caller doesn't know MPI communicator, so use MPI_COMM_WORLD
   initialize MPI if needed
------------------------------------------------------------------------- */

void lammps_open_no_mpi(int argc, char **argv, void **ptr)
{
  int flag;
  MPI_Initialized(&flag);

  if (!flag) {
    int argc = 0;
    char **argv = NULL;
    MPI_Init(&argc,&argv);
  }

  MPI_Comm communicator = MPI_COMM_WORLD;

#ifdef LAMMPS_EXCEPTIONS
  try
  {
    LAMMPS *lmp = new LAMMPS(argc,argv,communicator);
    *ptr = (void *) lmp;
  }
  catch(LAMMPSException & e) {
    fprintf(stderr, "LAMMPS Exception: %s", e.message.c_str());
    *ptr = (void*) NULL;
  }
#else
  LAMMPS *lmp = new LAMMPS(argc,argv,communicator);
  *ptr = (void *) lmp;
#endif
}

/* ----------------------------------------------------------------------
   destruct an instance of LAMMPS
------------------------------------------------------------------------- */

void lammps_close(void *ptr)
{
  LAMMPS *lmp = (LAMMPS *) ptr;
  delete lmp;
}

/* ----------------------------------------------------------------------
   get the numerical representation of the current LAMMPS version
------------------------------------------------------------------------- */

int lammps_version(void *ptr)
{
  LAMMPS *lmp = (LAMMPS *) ptr;
  return atoi(lmp->universe->num_ver);
}

/* ----------------------------------------------------------------------
   process an input script in filename str
------------------------------------------------------------------------- */

void lammps_file(void *ptr, char *str)
{
  LAMMPS *lmp = (LAMMPS *) ptr;

  BEGIN_CAPTURE
  {
    lmp->input->file(str);
  }
  END_CAPTURE
}

/* ----------------------------------------------------------------------
   process a single input command in str
   does not matter if str ends in newline
   return command name to caller
------------------------------------------------------------------------- */

char *lammps_command(void *ptr, char *str)
{
  LAMMPS *lmp = (LAMMPS *) ptr;
  char *result = NULL;

  BEGIN_CAPTURE
  {
    result = lmp->input->one(str);
  }
  END_CAPTURE

  return result;
}

/* ----------------------------------------------------------------------
   process multiple input commands in cmds = list of strings
   does not matter if each string ends in newline
   create long contatentated string for processing by commands_string()
   insert newlines in concatenated string as needed
------------------------------------------------------------------------- */

void lammps_commands_list(void *ptr, int ncmd, char **cmds)
{
  LAMMPS *lmp = (LAMMPS *) ptr;

  int n = ncmd+1;
  for (int i = 0; i < ncmd; i++) n += strlen(cmds[i]);

  char *str = (char *) lmp->memory->smalloc(n,"lib/commands/list:str");
  str[0] = '\0';
  n = 0;

  for (int i = 0; i < ncmd; i++) {
    strcpy(&str[n],cmds[i]);
    n += strlen(cmds[i]);
    if (str[n-1] != '\n') {
      str[n] = '\n';
      str[n+1] = '\0';
      n++;
    }
  }

  lammps_commands_string(ptr,str);
  lmp->memory->sfree(str);
}

/* ----------------------------------------------------------------------
   process multiple input commands in single long str, separated by newlines
   single command can span multiple lines via continuation characters 
   multi-line commands enabled by triple quotes will not work
------------------------------------------------------------------------- */

void lammps_commands_string(void *ptr, char *str)
{
  LAMMPS *lmp = (LAMMPS *) ptr;

  // make copy of str so can strtok() it

  int n = strlen(str) + 1;
  char *copy = new char[n];
  strcpy(copy,str);

  BEGIN_CAPTURE
  {
    char *ptr = strtok(copy,"\n");
    if (ptr) concatenate_lines(ptr);
    while (ptr) {
      lmp->input->one(ptr);
      ptr = strtok(NULL,"\n");
      if (ptr) concatenate_lines(ptr);
    }
  }
  END_CAPTURE

  delete [] copy;
}

/* ----------------------------------------------------------------------
   clean-up function to free memory allocated by lib and returned to caller
------------------------------------------------------------------------- */

void lammps_free(void *ptr)
{
  free(ptr);
}

// ----------------------------------------------------------------------
// library API functions to extract info from LAMMPS or set info in LAMMPS
// ----------------------------------------------------------------------

/* ----------------------------------------------------------------------
   add LAMMPS-specific library functions
   all must receive LAMMPS pointer as argument
   customize by adding a function here and in library.h header file
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   extract a LAMMPS setting as an integer
   only use for settings that require return of an int
   customize by adding names
------------------------------------------------------------------------- */

int lammps_extract_setting(void *ptr, char *name)
{
  if (strcmp(name,"bigint") == 0) return sizeof(bigint);
  if (strcmp(name,"tagint") == 0) return sizeof(tagint);
  if (strcmp(name,"imageint") == 0) return sizeof(imageint);

  return -1;
}

/* ----------------------------------------------------------------------
   extract a pointer to an internal LAMMPS global entity
   name = desired quantity, e.g. dt or boxyhi or natoms
   returns a void pointer to the entity
     which the caller can cast to the proper data type
   returns a NULL if name not listed below
   this function need only be invoked once
     the returned pointer is a permanent valid reference to the quantity
   customize by adding names
------------------------------------------------------------------------- */

void *lammps_extract_global(void *ptr, char *name)
{
  LAMMPS *lmp = (LAMMPS *) ptr;

  if (strcmp(name,"dt") == 0) return (void *) &lmp->update->dt;
  if (strcmp(name,"boxlo") == 0) return (void *) lmp->domain->boxlo;
  if (strcmp(name,"boxhi") == 0) return (void *) lmp->domain->boxhi;
  if (strcmp(name,"boxxlo") == 0) return (void *) &lmp->domain->boxlo[0];
  if (strcmp(name,"boxxhi") == 0) return (void *) &lmp->domain->boxhi[0];
  if (strcmp(name,"boxylo") == 0) return (void *) &lmp->domain->boxlo[1];
  if (strcmp(name,"boxyhi") == 0) return (void *) &lmp->domain->boxhi[1];
  if (strcmp(name,"boxzlo") == 0) return (void *) &lmp->domain->boxlo[2];
  if (strcmp(name,"boxzhi") == 0) return (void *) &lmp->domain->boxhi[2];
  if (strcmp(name,"periodicity") == 0) return (void *) lmp->domain->periodicity;

  if (strcmp(name,"xy") == 0) return (void *) &lmp->domain->xy;
  if (strcmp(name,"xz") == 0) return (void *) &lmp->domain->xz;
  if (strcmp(name,"yz") == 0) return (void *) &lmp->domain->yz;
  if (strcmp(name,"natoms") == 0) return (void *) &lmp->atom->natoms;
  if (strcmp(name,"nbonds") == 0) return (void *) &lmp->atom->nbonds;
  if (strcmp(name,"nangles") == 0) return (void *) &lmp->atom->nangles;
  if (strcmp(name,"ndihedrals") == 0) return (void *) &lmp->atom->ndihedrals;
  if (strcmp(name,"nimpropers") == 0) return (void *) &lmp->atom->nimpropers;
  if (strcmp(name,"nlocal") == 0) return (void *) &lmp->atom->nlocal;
  if (strcmp(name,"nghost") == 0) return (void *) &lmp->atom->nghost;
  if (strcmp(name,"nmax") == 0) return (void *) &lmp->atom->nmax;
  if (strcmp(name,"ntimestep") == 0) return (void *) &lmp->update->ntimestep;

  if (strcmp(name,"units") == 0) return (void *) lmp->update->unit_style;
  if (strcmp(name,"triclinic") == 0) return (void *) &lmp->domain->triclinic;

  if (strcmp(name,"q_flag") == 0) return (void *) &lmp->atom->q_flag;

  // update->atime can be referenced as a pointer
  // thermo "timer" data cannot be, since it is computed on request
  // lammps_get_thermo() can access all thermo keywords by value

  if (strcmp(name,"atime") == 0) return (void *) &lmp->update->atime;
  if (strcmp(name,"atimestep") == 0) return (void *) &lmp->update->atimestep;

  return NULL;
}

/* ----------------------------------------------------------------------
   extract simulation box parameters
   see domain.h for definition of these arguments
   domain->init() call needed to set box_change
------------------------------------------------------------------------- */

void lammps_extract_box(void *ptr, double *boxlo, double *boxhi,
                        double *xy, double *yz, double *xz,
                        int *periodicity, int *box_change)
{
  LAMMPS *lmp = (LAMMPS *) ptr;
  Domain *domain = lmp->domain;
  domain->init();

  boxlo[0] = domain->boxlo[0];
  boxlo[1] = domain->boxlo[1];
  boxlo[2] = domain->boxlo[2];
  boxhi[0] = domain->boxhi[0];
  boxhi[1] = domain->boxhi[1];
  boxhi[2] = domain->boxhi[2];

  *xy = domain->xy;
  *yz = domain->yz;
  *xz = domain->xz;

  periodicity[0] = domain->periodicity[0];
  periodicity[1] = domain->periodicity[1];
  periodicity[2] = domain->periodicity[2];
  
  *box_change = domain->box_change;
}

/* ----------------------------------------------------------------------
   extract a pointer to an internal LAMMPS atom-based entity
   name = desired quantity, e.g. x or mass
   returns a void pointer to the entity
     which the caller can cast to the proper data type
   returns a NULL if Atom::extract() does not recognize the name
   the returned pointer is not a permanent valid reference to the
     per-atom quantity, since LAMMPS may reallocate per-atom data
   customize by adding names to Atom::extract()
------------------------------------------------------------------------- */

void *lammps_extract_atom(void *ptr, char *name)
{
  LAMMPS *lmp = (LAMMPS *) ptr;
  return lmp->atom->extract(name);
}

/* ----------------------------------------------------------------------
   extract a pointer to an internal LAMMPS compute-based entity
   the compute is invoked if its value(s) is not current
   id = compute ID
   style = 0 for global data, 1 for per-atom data, 2 for local data
   type = 0 for scalar, 1 for vector, 2 for array
   for global data, returns a pointer to the
     compute's internal data structure for the entity
     caller should cast it to (double *) for a scalar or vector
     caller should cast it to (double **) for an array
   for per-atom or local data, returns a pointer to the
     compute's internal data structure for the entity
     caller should cast it to (double *) for a vector
     caller should cast it to (double **) for an array
   returns a void pointer to the compute's internal data structure
     for the entity which the caller can cast to the proper data type
   returns a NULL if id is not recognized or style/type not supported
   the returned pointer is not a permanent valid reference to the
     compute data, this function should be re-invoked
   IMPORTANT: if the compute is not current it will be invoked
     LAMMPS cannot easily check here if it is valid to invoke the compute,
     so caller must insure that it is OK
------------------------------------------------------------------------- */

void *lammps_extract_compute(void *ptr, char *id, int style, int type)
{
  LAMMPS *lmp = (LAMMPS *) ptr;

  BEGIN_CAPTURE
  {
    int icompute = lmp->modify->find_compute(id);
    if (icompute < 0) return NULL;
    Compute *compute = lmp->modify->compute[icompute];

    if (style == 0) {
      if (type == 0) {
        if (!compute->scalar_flag) return NULL;
        if (compute->invoked_scalar != lmp->update->ntimestep)
          compute->compute_scalar();
        return (void *) &compute->scalar;
      }
      if (type == 1) {
        if (!compute->vector_flag) return NULL;
        if (compute->invoked_vector != lmp->update->ntimestep)
          compute->compute_vector();
        return (void *) compute->vector;
      }
      if (type == 2) {
        if (!compute->array_flag) return NULL;
        if (compute->invoked_array != lmp->update->ntimestep)
          compute->compute_array();
        return (void *) compute->array;
      }
    }

    if (style == 1) {
      if (!compute->peratom_flag) return NULL;
      if (type == 1) {
        if (compute->invoked_peratom != lmp->update->ntimestep)
          compute->compute_peratom();
        return (void *) compute->vector_atom;
      }
      if (type == 2) {
        if (compute->invoked_peratom != lmp->update->ntimestep)
          compute->compute_peratom();
        return (void *) compute->array_atom;
      }
    }

    if (style == 2) {
      if (!compute->local_flag) return NULL;
      if (type == 1) {
        if (compute->invoked_local != lmp->update->ntimestep)
          compute->compute_local();
        return (void *) compute->vector_local;
      }
      if (type == 2) {
        if (compute->invoked_local != lmp->update->ntimestep)
          compute->compute_local();
        return (void *) compute->array_local;
      }
    }
  }
  END_CAPTURE

  return NULL;
}

/* ----------------------------------------------------------------------
   extract a pointer to an internal LAMMPS fix-based entity
   id = fix ID
   style = 0 for global data, 1 for per-atom data, 2 for local data
   type = 0 for scalar, 1 for vector, 2 for array
   i,j = indices needed only to specify which global vector or array value
   for global data, returns a pointer to a memory location
     which is allocated by this function
     which the caller can cast to a (double *) which points to the value
   for per-atom or local data, returns a pointer to the
     fix's internal data structure for the entity
     caller should cast it to (double *) for a vector
     caller should cast it to (double **) for an array
   returns a NULL if id is not recognized or style/type not supported
   IMPORTANT: for global data,
     this function allocates a double to store the value in,
     so the caller must free this memory to avoid a leak, e.g.
       double *dptr = (double *) lammps_extract_fix();
       double value = *dptr;
       lammps_free(dptr);
   IMPORTANT: LAMMPS cannot easily check here when info extracted from
     the fix is valid, so caller must insure that it is OK
------------------------------------------------------------------------- */

void *lammps_extract_fix(void *ptr, char *id, int style, int type,
                         int i, int j)
{
  LAMMPS *lmp = (LAMMPS *) ptr;

  BEGIN_CAPTURE
  {
    int ifix = lmp->modify->find_fix(id);
    if (ifix < 0) return NULL;
    Fix *fix = lmp->modify->fix[ifix];

    if (style == 0) {
      double *dptr = (double *) malloc(sizeof(double));
      if (type == 0) {
        if (!fix->scalar_flag) return NULL;
        *dptr = fix->compute_scalar();
        return (void *) dptr;
      }
      if (type == 1) {
        if (!fix->vector_flag) return NULL;
        *dptr = fix->compute_vector(i);
        return (void *) dptr;
      }
      if (type == 2) {
        if (!fix->array_flag) return NULL;
        *dptr = fix->compute_array(i,j);
        return (void *) dptr;
      }
    }

    if (style == 1) {
      if (!fix->peratom_flag) return NULL;
      if (type == 1) return (void *) fix->vector_atom;
      if (type == 2) return (void *) fix->array_atom;
    }

    if (style == 2) {
      if (!fix->local_flag) return NULL;
      if (type == 1) return (void *) fix->vector_local;
      if (type == 2) return (void *) fix->array_local;
    }
  }
  END_CAPTURE

  return NULL;
}

/* ----------------------------------------------------------------------
   extract a pointer to an internal LAMMPS evaluated variable
   name = variable name, must be equal-style or atom-style variable
   group = group ID for evaluating an atom-style variable, else NULL
   for equal-style variable, returns a pointer to a memory location
     which is allocated by this function
     which the caller can cast to a (double *) which points to the value
   for atom-style variable, returns a pointer to the
     vector of per-atom values on each processor,
     which the caller can cast to a (double *) which points to the values
   returns a NULL if name is not recognized or not equal-style or atom-style
   IMPORTANT: for both equal-style and atom-style variables,
     this function allocates memory to store the variable data in
     so the caller must free this memory to avoid a leak
     e.g. for equal-style variables
       double *dptr = (double *) lammps_extract_variable();
       double value = *dptr;
       lammps_free(dptr);
     e.g. for atom-style variables
       double *vector = (double *) lammps_extract_variable();
       use the vector values
       lammps_free(vector);
   IMPORTANT: LAMMPS cannot easily check here when it is valid to evaluate
     the variable or any fixes or computes or thermodynamic info it references,
     so caller must insure that it is OK
------------------------------------------------------------------------- */

void *lammps_extract_variable(void *ptr, char *name, char *group)
{
  LAMMPS *lmp = (LAMMPS *) ptr;

  BEGIN_CAPTURE
  {
    int ivar = lmp->input->variable->find(name);
    if (ivar < 0) return NULL;

    if (lmp->input->variable->equalstyle(ivar)) {
      double *dptr = (double *) malloc(sizeof(double));
      *dptr = lmp->input->variable->compute_equal(ivar);
      return (void *) dptr;
    }

    if (lmp->input->variable->atomstyle(ivar)) {
      int igroup = lmp->group->find(group);
      if (igroup < 0) return NULL;
      int nlocal = lmp->atom->nlocal;
      double *vector = (double *) malloc(nlocal*sizeof(double));
      lmp->input->variable->compute_atom(ivar,igroup,vector,1,0);
      return (void *) vector;
    }
  }
  END_CAPTURE

  return NULL;
}


/* ----------------------------------------------------------------------
   reset simulation box parameters
   see domain.h for definition of these arguments
   assumes domain->set_initial_box() has been invoked previously
------------------------------------------------------------------------- */

void lammps_reset_box(void *ptr, double *boxlo, double *boxhi,
                      double xy, double yz, double xz)
{
  LAMMPS *lmp = (LAMMPS *) ptr;
  Domain *domain = lmp->domain;

  domain->boxlo[0] = boxlo[0];
  domain->boxlo[1] = boxlo[1];
  domain->boxlo[2] = boxlo[2];
  domain->boxhi[0] = boxhi[0];
  domain->boxhi[1] = boxhi[1];
  domain->boxhi[2] = boxhi[2];

  domain->xy = xy;
  domain->yz = yz;
  domain->xz = xz;

  domain->set_global_box();
  lmp->comm->set_proc_grid();
  domain->set_local_box();
}

/* ----------------------------------------------------------------------
   set the value of a STRING variable to str
   return -1 if variable doesn't exist or not a STRING variable
   return 0 for success
------------------------------------------------------------------------- */

int lammps_set_variable(void *ptr, char *name, char *str)
{
  LAMMPS *lmp = (LAMMPS *) ptr;
  int err = -1;

  BEGIN_CAPTURE
  {
    err = lmp->input->variable->set_string(name,str);
  }
  END_CAPTURE

  return err;
}

/* ----------------------------------------------------------------------
   return the current value of a thermo keyword as a double
   unlike lammps_extract_global() this does not give access to the
     storage of the data in question
   instead it triggers the Thermo class to compute the current value
     and returns it
------------------------------------------------------------------------- */

double lammps_get_thermo(void *ptr, char *name)
{
  LAMMPS *lmp = (LAMMPS *) ptr;
  double dval = 0.0;

  BEGIN_CAPTURE
  {
    lmp->output->thermo->evaluate_keyword(name,&dval);
  }
  END_CAPTURE

  return dval;
}

/* ----------------------------------------------------------------------
   return the total number of atoms in the system
   useful before call to lammps_get_atoms() so can pre-allocate vector
------------------------------------------------------------------------- */

int lammps_get_natoms(void *ptr)
{
  LAMMPS *lmp = (LAMMPS *) ptr;

  if (lmp->atom->natoms > MAXSMALLINT) return 0;
  int natoms = static_cast<int> (lmp->atom->natoms);
  return natoms;
}

/* ----------------------------------------------------------------------
   gather the named atom-based entity across all processors
   atom IDs must be consecutive from 1 to N
   name = desired quantity, e.g. x or charge
   type = 0 for integer values, 1 for double values
   count = # of per-atom values, e.g. 1 for type or charge, 3 for x or f
   return atom-based values in 1d data, ordered by count, then by atom ID
     e.g. x[0][0],x[0][1],x[0][2],x[1][0],x[1][1],x[1][2],x[2][0],...
     data must be pre-allocated by caller to correct length
------------------------------------------------------------------------- */

void lammps_gather_atoms(void *ptr, char *name,
                         int type, int count, void *data)
{
  LAMMPS *lmp = (LAMMPS *) ptr;

  BEGIN_CAPTURE
  {
    // error if tags are not defined or not consecutive

    int flag = 0;
    if (lmp->atom->tag_enable == 0 || lmp->atom->tag_consecutive() == 0) 
      flag = 1;
    if (lmp->atom->natoms > MAXSMALLINT) flag = 1;
    if (flag) {
      if (lmp->comm->me == 0)
        lmp->error->warning(FLERR,"Library error in lammps_gather_atoms");
      return;
    }

    int natoms = static_cast<int> (lmp->atom->natoms);

    int i,j,offset;
    void *vptr = lmp->atom->extract(name);
    if(vptr == NULL) {
        lmp->error->warning(FLERR,"lammps_gather_atoms: unknown property name");
        return;
    }

    // copy = Natom length vector of per-atom values
    // use atom ID to insert each atom's values into copy
    // MPI_Allreduce with MPI_SUM to merge into data, ordered by atom ID

    if (type == 0) {
      int *vector = NULL;
      int **array = NULL;
      if (count == 1) vector = (int *) vptr;
      else array = (int **) vptr;

      int *copy;
      lmp->memory->create(copy,count*natoms,"lib/gather:copy");
      for (i = 0; i < count*natoms; i++) copy[i] = 0;

      tagint *tag = lmp->atom->tag;
      int nlocal = lmp->atom->nlocal;

      if (count == 1)
        for (i = 0; i < nlocal; i++)
          copy[tag[i]-1] = vector[i];
      else
        for (i = 0; i < nlocal; i++) {
          offset = count*(tag[i]-1);
          for (j = 0; j < count; j++)
            copy[offset++] = array[i][0];
        }
      
      MPI_Allreduce(copy,data,count*natoms,MPI_INT,MPI_SUM,lmp->world);
      lmp->memory->destroy(copy);

    } else {
      double *vector = NULL;
      double **array = NULL;
      if (count == 1) vector = (double *) vptr;
      else array = (double **) vptr;

      double *copy;
      lmp->memory->create(copy,count*natoms,"lib/gather:copy");
      for (i = 0; i < count*natoms; i++) copy[i] = 0.0;

      tagint *tag = lmp->atom->tag;
      int nlocal = lmp->atom->nlocal;

      if (count == 1) {
        for (i = 0; i < nlocal; i++)
          copy[tag[i]-1] = vector[i];
      } else {
        for (i = 0; i < nlocal; i++) {
          offset = count*(tag[i]-1);
          for (j = 0; j < count; j++)
            copy[offset++] = array[i][j];
        }
      }

      MPI_Allreduce(copy,data,count*natoms,MPI_DOUBLE,MPI_SUM,lmp->world);
      lmp->memory->destroy(copy);
    }
  }
  END_CAPTURE
}

/* ----------------------------------------------------------------------
   scatter the named atom-based entity across all processors
   atom IDs must be consecutive from 1 to N
   name = desired quantity, e.g. x or charge
   type = 0 for integer values, 1 for double values
   count = # of per-atom values, e.g. 1 for type or charge, 3 for x or f
   data = atom-based values in 1d data, ordered by count, then by atom ID
     e.g. x[0][0],x[0][1],x[0][2],x[1][0],x[1][1],x[1][2],x[2][0],...
------------------------------------------------------------------------- */

void lammps_scatter_atoms(void *ptr, char *name,
                          int type, int count, void *data)
{
  LAMMPS *lmp = (LAMMPS *) ptr;

  BEGIN_CAPTURE
  {
    // error if tags are not defined or not consecutive or no atom map

    int flag = 0;
    if (lmp->atom->tag_enable == 0 || lmp->atom->tag_consecutive() == 0) 
      flag = 1;
    if (lmp->atom->natoms > MAXSMALLINT) flag = 1;
    if (lmp->atom->map_style == 0) flag = 1;
    if (flag) {
      if (lmp->comm->me == 0)
        lmp->error->warning(FLERR,"Library error in lammps_scatter_atoms");
      return;
    }

    int natoms = static_cast<int> (lmp->atom->natoms);

    int i,j,m,offset;
    void *vptr = lmp->atom->extract(name);
    if(vptr == NULL) {
        lmp->error->warning(FLERR,
                            "lammps_scatter_atoms: unknown property name");
        return;
    }

    // copy = Natom length vector of per-atom values
    // use atom ID to insert each atom's values into copy
    // MPI_Allreduce with MPI_SUM to merge into data, ordered by atom ID

    if (type == 0) {
      int *vector = NULL;
      int **array = NULL;
      if (count == 1) vector = (int *) vptr;
      else array = (int **) vptr;
      int *dptr = (int *) data;

      if (count == 1) {
        for (i = 0; i < natoms; i++)
          if ((m = lmp->atom->map(i+1)) >= 0)
            vector[m] = dptr[i];
      } else {
        for (i = 0; i < natoms; i++)
          if ((m = lmp->atom->map(i+1)) >= 0) {
            offset = count*i;
            for (j = 0; j < count; j++)
              array[m][j] = dptr[offset++];
          }
      }
    } else {
      double *vector = NULL;
      double **array = NULL;
      if (count == 1) vector = (double *) vptr;
      else array = (double **) vptr;
      double *dptr = (double *) data;

      if (count == 1) {
        for (i = 0; i < natoms; i++)
          if ((m = lmp->atom->map(i+1)) >= 0)
            vector[m] = dptr[i];
      } else {
        for (i = 0; i < natoms; i++) {
          if ((m = lmp->atom->map(i+1)) >= 0) {
            offset = count*i;
            for (j = 0; j < count; j++)
              array[m][j] = dptr[offset++];
          }
        }
      }
    }
  }
  END_CAPTURE
}

/* ----------------------------------------------------------------------
   create N atoms and assign them to procs based on coords
   id = atom IDs (optional, NULL will generate 1 to N)
   type = N-length vector of atom types (required)
   x = 3N-length 1d vector of atom coords (required)
   v = 3N-length 1d vector of atom velocities (optional, NULL if just 0.0)
   image flags can be treated in two ways:
     (a) image = vector of current image flags
         each atom will be remapped into periodic box by domain->ownatom()
         image flag will be incremented accordingly and stored with atom
     (b) image = NULL
         each atom will be remapped into periodic box by domain->ownatom()
         image flag will be set to 0 by atom->avec->create_atom()
   shrinkexceed = 1 allows atoms to be outside a shrinkwrapped boundary
     passed to ownatom() which will assign them to boundary proc
     important if atoms may be (slightly) outside non-periodic dim
     e.g. due to restoring a snapshot from a previous run and previous box
   id and image must be 32-bit integers
   x,v = ordered by xyz, then by atom
     e.g. x[0][0],x[0][1],x[0][2],x[1][0],x[1][1],x[1][2],x[2][0],...
------------------------------------------------------------------------- */

void lammps_create_atoms(void *ptr, int n, tagint *id, int *type,
			 double *x, double *v, imageint *image,
                         int shrinkexceed)
{
  LAMMPS *lmp = (LAMMPS *) ptr;

  BEGIN_CAPTURE
  {
    // error if box does not exist or tags not defined

    int flag = 0;
    if (lmp->domain->box_exist == 0) flag = 1;
    if (lmp->atom->tag_enable == 0) flag = 1;
    if (flag) {
      if (lmp->comm->me == 0)
        lmp->error->warning(FLERR,"Library error in lammps_create_atoms");
      return;
    }

    // loop over N atoms of entire system
    // if this proc owns it based on coords, invoke create_atom()
    // optionally set atom tags and velocities

    Atom *atom = lmp->atom;
    Domain *domain = lmp->domain;
    int nlocal = atom->nlocal;

    bigint natoms_prev = atom->natoms;
    int nlocal_prev = nlocal;
    double xdata[3];
    
    for (int i = 0; i < n; i++) {
      xdata[0] = x[3*i];
      xdata[1] = x[3*i+1];
      xdata[2] = x[3*i+2];
      imageint * img = image ? &image[i] : NULL;
      tagint     tag = id    ? id[i]     : -1;
      if (!domain->ownatom(tag, xdata, img, shrinkexceed)) continue;
  
      atom->avec->create_atom(type[i],xdata);
      if (id) atom->tag[nlocal] = id[i];
      else atom->tag[nlocal] = i+1;
      if (v) {
	atom->v[nlocal][0] = v[3*i];
	atom->v[nlocal][1] = v[3*i+1];
	atom->v[nlocal][2] = v[3*i+2];
      }
      if (image) atom->image[nlocal] = image[i];
      nlocal++;
    }

    // need to reset atom->natoms inside LAMMPS

    bigint ncurrent = nlocal;
    MPI_Allreduce(&ncurrent,&lmp->atom->natoms,1,MPI_LMP_BIGINT,
                  MPI_SUM,lmp->world);

    // init per-atom fix/compute/variable values for created atoms

    atom->data_fix_compute_variable(nlocal_prev,nlocal);

    // if global map exists, reset it
    // invoke map_init() b/c atom count has grown

    if (lmp->atom->map_style) {
      lmp->atom->map_init();
      lmp->atom->map_set();
    }

    // warn if new natoms is not correct
    
    if (lmp->atom->natoms != natoms_prev + n) {
      char str[128];
      sprintf(str,"Library warning in lammps_create_atoms, "
              "invalid total atoms %ld %ld",lmp->atom->natoms,natoms_prev+n);
      if (lmp->comm->me == 0)
        lmp->error->warning(FLERR,str);
    }
  }
  END_CAPTURE
}

// ----------------------------------------------------------------------
// library API functions for error handling
// ----------------------------------------------------------------------

#ifdef LAMMPS_EXCEPTIONS

/* ----------------------------------------------------------------------
   check if a new error message
------------------------------------------------------------------------- */

int lammps_has_error(void *ptr) {
  LAMMPS *  lmp = (LAMMPS *) ptr;
  Error * error = lmp->error;
  return error->get_last_error() ? 1 : 0;
}

/* ----------------------------------------------------------------------
   copy the last error message of LAMMPS into a character buffer
   return value encodes which type of error:
   1 = normal error (recoverable)
   2 = abort error (non-recoverable)
------------------------------------------------------------------------- */

int lammps_get_last_error_message(void *ptr, char * buffer, int buffer_size) {
  LAMMPS *  lmp = (LAMMPS *) ptr;
  Error * error = lmp->error;

  if(error->get_last_error()) {
    int error_type = error->get_last_error_type();
    strncpy(buffer, error->get_last_error(), buffer_size-1);
    error->set_last_error(NULL, ERROR_NONE);
    return error_type;
  }
  return 0;
}

#endif
