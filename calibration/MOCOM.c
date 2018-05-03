#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <assert.h>
#include <stdarg.h>
#include "MOCOM.h"


/* messy, but now the interfaces are cleaner, and most of this -is- global */
/* config */
int N_RAND, N_SET, N_PARAM, N_TEST_FUNCS;
PARAM_RANGE *param_lim;
const char *labelstr, *runstr;
/* internal */
ITEM * set;
FILE *fopti;
float * prob;
int Rmax, N_Rmax, generation;

int main(int argc,char ** argv) {
/**********************************************************************
  MOCOM-UA.c             Keith Cherkauer            November 18, 1999

  This program optimizes the provided model script using the 
  multi-objective global optimization method introduced in:
  Yapo, P. O., H. V. Gupta, and S. Sorooshian, "Multi-objective
  global optimization for hydrologic models," J. Hydrol., 204,
  1998, pp. 83-97.

  The program requires:
  (1) the name of a script which will change the parameters of the 
  model, run it, route the results, and compute an R^2 value (times 
  -1 so that an R^2 of -1 is a perfect fit) which is then returned 
  to this program and used to find optimized parameters.

  (2) a prefix unique to this run of the program with which to create
  a temporary file.

  (3) the name of a log file to which it can write the results of
  all optimization attempts.

  Modifications:
  09252000 Modified to let the user define an initial population larger
    than the one actually used to find the Pareto line.  The program
    first determines the results for NUM_RAND random starts, ranks and
    sorts those starts and then optimizes the NUM_SET best values
    from the random start.                                        KAC
  01102001 Modified so that the optimizer can be restarted if provided
    with a file containing a list of parameters, their resulting test
    values and the storage directory if used in the previous 
    simulations.                                                  KAC
  05152003 Modified to store every 100 solution sets in a new 
    subdirectory (sets 0-99 are in SAVE_0, sets 100-199 are in SAVE_100).
    The optimizer also removes all sets that were not saved in an
    output generation list, which should reduce the number of saved
    sets.                                                         KAC

**********************************************************************/

  int              RESTART;
/*  int              iter;   TODO rename me and use me! */
  const char      *param_lim_file, *restart_file;
  int              FOUND_BETTER;
/*  int              cycle;  */ /* AWW-test param   --  TODO reinstate me*/
  time_t           currtime, tmptime;
  long            *ran2seed;
  AMOEBA_CONTEXT  *acontext;
  char             cmdstr[1024];

  if ( argc != 9 ) usage();
  
  if ( argv[1][0] >= '0' && argv[1][0] <= '9' ) { /* random start selected */
    RESTART = FALSE;
    if((N_RAND = atoi(argv[1])) > MAX_SET)
      mocom_die("Requested population size %i greater than MAX_SET (%i).\nReduce population or recompile optimizer.\n",N_RAND,MAX_SET);
    restart_file = NULL;
  } else {
    RESTART = TRUE;
    restart_file = argv[1];
  }

  if((N_SET = atoi(argv[2])) > MAX_SET)
    mocom_die("Requested population size %i greater than MAX_SET (%i).\nReduce population or recompile optimizer.\n",N_SET,MAX_SET);

  if((N_PARAM = atoi(argv[3])) > MAX_PARAM)
    mocom_die("Requested number of parameters %i, is larger than MAX_PARAM (%i).\nReduce the number of parameters or recompile the optimizer.\n",N_PARAM,MAX_PARAM);

  if((N_TEST_FUNCS = atoi(argv[4])) > MAX_TEST_FUNCS)
    mocom_die("Requested number of test functions (%i) is greater than MAX_TEST_FUNCS (%i).\nReduce number of test functions or recompile optimizer.\n",N_TEST_FUNCS,MAX_TEST_FUNCS);

  runstr = argv[5];

  labelstr = argv[6];

  if((fopti=fopen(argv[7],"w"))==NULL)
    mocom_die("Unable to open optimization log file %s.\n", argv[7]);

  param_lim_file = argv[8];


  /**  Banner  **/
  printf("Concurrent (PBS) MOCOM optimizer, built " __DATE__ "\n\n");
  printf( RESTART ?
            "  Optimization restart filename:             %s\n"
          : "  Initial parameter set population size:     %s\n", argv[1]);
  printf(   "  Evolution parameter set population size:   %s\n"
            "  Number of calibration parameters:          %s (note that some of these may be invariant)\n"
            "  Number of statistical tests:               %s\n"
            "  Model invocation script:                   %s\n"
            "  Calibration run identity string:           %s\n"
            "  Optimization log file:                     %s\n"
            "  Parameter range file:                      %s\n\n\n",
            argv[2], argv[3], argv[4], argv[5], argv[6], argv[7], argv[8] ) ;

  /**  Seed the random number generator  **/
  tmptime     = time(&currtime) * -1;
  ran2seed    = &tmptime;
  ran2(ran2seed);
  ran2seed[0] = 1;

  /**  Initialize variables and arrays  **/
  SOLVE_CNT   = 0; /* note this value is skipped over */
  generation  = 0;

  set      = malloc(MAX_SET * sizeof(*set));
  acontext = malloc(MAX_SET * sizeof(*acontext));
  prob     = malloc(N_SET*sizeof(float));
  param_lim = set_param_limits(param_lim_file,N_PARAM);

  /** Initialize first generation **/
  if ( RESTART ) restart_optimization(restart_file);
  else random_start_optimization(ran2seed);
  printf("Initial set generation finished.\n\nStarting tests:\n\n"); 
  
printf("Line check 1.\n");
  
  while ( (Rmax > MAX_RANK)  /* AWW added: */ &&  ((N_SET - N_Rmax) > N_PARAM) )  /* PN: Confirmed: can usually be avoided, increase N_SET */
  {
    if(generation % PRT_GENERATION == 0) {
      /** Print current generation for external monitoring **/
      fprintf(fopti,"\nCurrent generation for generation %i:\n\n", generation);
      printf("\nCurrent generation for generation %i:\n\n", generation);
      /*header row*/  /* TODO the on-screen column alignment here breaks */
      for ( int j = 0; j < N_PARAM; j++ ) 
        fprintf(fopti,"\t%s",param_lim[j].name); 
      for ( int j = 0; j < N_TEST_FUNCS; j++ ) 
        fprintf(fopti,"\ttest%d",j);
      fprintf(fopti," \trank\tsoln_num\n\n");
      /*run specs*/
      for ( int i = 0; i < N_SET; i++) {
        fprintf(fopti,"%i:",i);
        for ( int j = 0; j < N_PARAM; j++ ) 
          fprintf(fopti,"\t%.5g",set[i].p[j]);
        fprintf(fopti,"\t= (");
        for ( int j = 0; j < N_TEST_FUNCS; j++ ) {
          fprintf(fopti,"\t%.5g",set[i].f[j]);
        }
        fprintf(fopti," )\t%i\t%i\n", set[i].rank, set[i].soln_num);

        /* Mark current point for saving -- NOTE: this only saves sets in PRINTED generations, but this is likely desirable behaviour */
printf("Line check 2.\n");

        sprintf( cmdstr, "touch runs/%s/%05i/.keep", labelstr, set[i].soln_num); //180424LML added 'touch'
        system( cmdstr );

      }
      fprintf(fopti,"\n");
      fflush(fopti);

      /* remove all sets that have not been output as part of a generatation */
      sprintf( cmdstr, "cd runs/%s/ ; for i in * ; do if [[ ( -d \"$i\" ) && ( ! -e \"$i\"/.keep ) ]] ; then rm -Rf \"$i\" ; fi ; done ; cd ../../..", labelstr );
      system( cmdstr );
      
      printf("Test functions evaluated %i times:  parameter set generation %i\n", SOLVE_CNT, generation);
    }

    fprintf(fopti,"==========\nTry %i has Rmax=%i ranks with N_Rmax=%i in the highest rank. Solving...\n", generation, Rmax, N_Rmax);
    printf(       "==========\nTry %i has Rmax=%i ranks with N_Rmax=%i in the highest rank. Solving...\n", generation, Rmax, N_Rmax);

    /***********************
    *  Init amoeba states  *
    ***********************/
    for ( int i = N_SET - N_Rmax; i < N_SET; i++ ) {   /* Loop through points to be evolved from */
      acontext[i].parent = set[i];
      acontext[i].exec_state = amoebadone; /* trigger simplex set population */
      acontext[i].FOUND = FALSE;
    }

    /************************
    *  Main evolution loop  *
    ************************/
    do {
      FOUND_BETTER = TRUE;

      for ( int i = N_SET - N_Rmax; i < N_SET; i++ ) {
        if ( acontext[i].exec_state != amoebadone ) {
          FOUND_BETTER = FALSE;
          amoeba(&acontext[i]);
        } else if ( !acontext[i].FOUND ) {
          /* simplex must be repopulated */
          FOUND_BETTER = FALSE;
          populate_simplex(acontext[i].test_set, ran2seed);
          acontext[i].exec_state = amoebauninitialized;
        }
      }
      sleep(5);
    } while (!FOUND_BETTER);

    /*  Copy evolved points into population  */
    for ( int i = N_SET - N_Rmax; i < N_SET; i++ )
      set[i] = acontext[i].spawn;

    /* TODO move use of 'iter' counter this to AMOEBA_CONTEXT or something? */

    generation++;

    Rmax = rank(set, N_SET);
    quick(set,N_SET);
    calc_rank_probs();

    /* AWW:  PN (paraphrase of AWW): test_set selection intractible if N_PARAM > (N_SET-N_Rmax): bail informatively.
     * 2009/04/08: PN: updated to only do this if the optimization is not complete, as this logic is otherwise at the wrong place in the code
     */
    if ((Rmax > MAX_RANK) && ((N_SET-N_Rmax) < N_PARAM)) {
      fprintf(fopti,  "MOCOM:  WARNING:  Current generation of %d sets has fewer sets (%d) of rank < Rmax(=%d) than N_PARAM=%d required for simplex evolution.\nCheck output -- you may have reached an acceptable calibration\n",N_SET, N_SET-N_Rmax, Rmax,N_PARAM);
      fprintf(stderr, "MOCOM:  WARNING:  Current generation of %d sets has fewer sets (%d) of rank < Rmax(=%d) than N_PARAM=%d required for simplex evolution.\nCheck output -- you may have reached an acceptable calibration\n",N_SET, N_SET-N_Rmax, Rmax,N_PARAM);
      exit(5);
    }
    
  }  /* while Rmax > MAX_RANK */

  /*
   * Optimization has been completed, output final generation
   */
printf("Line check 3a.\n");
  fprintf(fopti,"\nResults for multi-objective global optimization:\n");
  fprintf(fopti,"\tNeeded %i iterations to solve with a population of %i\n==========\n",generation,N_SET);
  /*header row*/
  for ( int j = 0; j < N_PARAM; j++) fprintf(fopti,"\t%s",param_lim[j].name);
  for ( int j = 0; j < N_TEST_FUNCS; j++ )    fprintf(fopti,"\ttest%d",j /* +1 */ /* this is inconsistent with output above */);
  fprintf(fopti," \trank\tsoln_num\n");
  fprintf(fopti,"\n");
  /* param & results specs */
  for ( int i = 0; i < N_SET; i++ ) {
    fprintf(fopti,"%i:",i);
    for ( int j = 0; j < N_PARAM; j++) 
      fprintf(fopti,"\t%.5g",set[i].p[j]);
    fprintf(fopti,"\t= (");
    for ( int j = 0; j < N_TEST_FUNCS; j++ )
      fprintf(fopti,"\t%.5g",set[i].f[j]);
    fprintf(fopti," )\t%i\t%i\n", set[i].rank, set[i].soln_num);
    /* identify current set for saving */
printf("Line check 3b. set=%d \n",set[i].soln_num);
    sprintf( cmdstr, "touch runs/%s/%05i/.keep", labelstr, set[i].soln_num);
    system( cmdstr );
  }
  fprintf(fopti,"\n");

  printf("Optimization required the function be evaluated %i times, through %i generations.\nDONE!\n", SOLVE_CNT, generation);

  return 0;
printf("Line check 4.\n");
}


void
populate_simplex(ITEM * test_set, long * ran2seed)
{
  int NEW_SET;

  /* BUG #0002: shifting down one to leave non-random param set in LAST position - this used to count 1=>N_PARAM*/
  for ( int j = 0; j <  N_PARAM; j++ ) {
    int cycle=0;
    do {
      float tmp_prob = ran2(ran2seed);
      int k = 0;
      while(tmp_prob>prob[k] && k<N_SET-1) k++;
      if ( k < (N_SET - N_Rmax) ) {
        test_set[j].pos = k;
        NEW_SET=TRUE;
        for ( int l = 0; l < j; l++ )
          if (test_set[l].pos==test_set[j].pos) NEW_SET=FALSE;
      } else
        NEW_SET=FALSE;

      assert(++cycle<10000); /* AWW  --  PN: this is related to AWW's other update, forcing termination when N_PARAM > N_SET-N_Rmax*/
    } while(!NEW_SET); /* repeat if the selected point was already in test_set */

    set[test_set[j].pos].pos = test_set[j].pos;          
    test_set[j] = set[test_set[j].pos];      
  }

  quick(test_set, N_PARAM);
printf("Line check 5.\n");
}


/*  Generates and tests (runs) N_RAND points, and culls to N_SET best points.  */
void random_start_optimization(long *ran2seed)
{
  char        cmdstr[1024];
  void      **queue = malloc(N_RAND * sizeof(*queue));

  /** formulate the original parameter set **/
  fprintf(fopti,"Determining starting parameters...\n==========\n");
  for(int j = 0; j < N_PARAM; j++) fprintf(fopti,"\t%s",param_lim[j].name);
  fprintf(fopti,"\n");

  /*  Randomly generate and dispatch parameter sets  */
  for (int setcnt = 0; setcnt < N_RAND; setcnt++ ) {
    for (int param = 0; param < N_PARAM; param++ )
      set[setcnt].p[param] = ( (param_lim[param].max - param_lim[param].min) * ran2(ran2seed) ) + param_lim[param].min;
    queue[setcnt] = dispatch_model(set[setcnt].p, set[setcnt].f, &set[setcnt].soln_num);
  }

  /*  Retrieve them, and mark for retention  */
  for (int setcnt = 0; setcnt < N_RAND; setcnt++ ) {
    fprintf(fopti,"%i:\t",setcnt);
    retrieve_model(queue[setcnt]);
printf("Line check 6.\n");
    sprintf( cmdstr, "touch runs/%s/%05i/.keep", labelstr, set[setcnt].soln_num);
    system( cmdstr );
  }
  
  /* Rank and sort randomly-generated set, cull/re-rank, re-sort, and calculate probabilities */
  Rmax = rank( set, N_RAND );
  quick( set, N_RAND );
  Rmax = rank( set, N_SET );
  quick( set, N_SET );
  calc_rank_probs();

  free(queue);
}


/*  Reads a population from a file  */
void restart_optimization(const char *filename)
{
  FILE       *f;

  if ( ( f = fopen(filename,"r") ) == NULL )
    mocom_die("Unable to open restart file %s", filename);

  /** formulate the original parameter set **/
  fprintf(fopti,"Reading starting parameters from %s...\n==========\n", filename);
  for ( int j = 0; j < N_PARAM; j++ ) fprintf(fopti, "\t%s", param_lim[j].name);
  fprintf(fopti,"\n");

  for ( int setcnt = 0; setcnt < N_SET; setcnt++ ) {
    fprintf(fopti, "%i:", setcnt);
    for ( int param = 0; param < N_PARAM; param++ ) {
      fscanf(f, "%f", &set[setcnt].p[param]);
      fprintf(fopti, "\t%f", set[setcnt].p[param]);
    }
    fprintf(fopti," \t=\t(");
    for ( int test = 0; test < N_TEST_FUNCS; test++ ) {
      fscanf(f, "%f", &set[setcnt].f[test]);
      fprintf(fopti, "\t%f", set[setcnt].p[test]);
    }
    fscanf(f, "%i", &set[setcnt].soln_num);
    set[setcnt].soln_num *= -1;
    fprintf(fopti, "\t)\t%i\n", set[setcnt].soln_num);      
  }
  fclose(f);
  
  Rmax = rank( set, N_SET );
  quick( set, N_SET );
  calc_rank_probs();
}


void amoeba ( AMOEBA_CONTEXT * a )
{
/***********************************************************************
  Simplex optimization routine from Numerical Recipies

  Modifications:
  11-18-99 Modifed so that the routine returns the first value, 
           instead of iterating to find the "best" value (as 
           described by Yapo et al. 1998).
  01-05-01 Modified to record the solution number as reference
           for stored information.

***********************************************************************/
 //switch(a->exec_state)  /* mwahahahaha. */
// {
//  case amoebadone:  assert(0);  /* ruh-roh */
//  case amoebauninitialized:
//
 if (a->exec_state == amoebadone) {
  fprintf(stderr, "a->exec_state == amoebadone!\n");
  assert(0);
 } else if (a->exec_state == amoebauninitialized) {  
   a->FOUND  = FALSE;

/*  ++*a->extern_iter; */  /* FIXME what are the semantics of this thing now? */
  
  /** Reflect simplex from the high point ---------------------- **/
  for ( int j = 0; j < N_PARAM; j++ ) a->pbar[j] = 0.0;
  for ( int i = 0; i < N_PARAM; i++ )
    for ( int j = 0; j < N_PARAM; j++ ) a->pbar[j] += a->test_set[i].p[j];
  for ( int j = 0; j < N_PARAM; j++ ) a->pbar[j] /= N_PARAM;

  /* Reflect high point across average */
  for ( int j = 0; j < N_PARAM; j++ ) a->r.p[j] = (1.0+ALPHA)*a->pbar[j] - ALPHA*a->parent.p[j];

  a->dispatch_state = dispatch_model( a->r.p, a->r.f, &a->r.soln_num );
  //a->exec_state = amoeba1;
//case amoeba1: if (!check_model(a->dispatch_state)) return;  /* all of these should normally be returning on fall-through */
  if (! check_model(a->dispatch_state)) { //good parameter
    retrieve_model(a->dispatch_state);
    if ( less_than_or_equal(a->r.f,a->test_set[0].f,N_TEST_FUNCS) ) {
      /** Solution better than best point, so try additional extrapolation by a factor of GAMMA **/
      for ( int j = 0; j < N_PARAM; j++ ) a->rr.p[j] = GAMMA*a->r.p[j] + (1.0-GAMMA)*a->pbar[j];
      a->dispatch_state = dispatch_model( a->rr.p, a->rr.f, &a->rr.soln_num );
      //a->exec_state = amoeba2;
      //case amoeba2: if (!check_model(a->dispatch_state)) return;
      if (! check_model(a->dispatch_state)) { //good parameter    
        retrieve_model(a->dispatch_state);
        if ( less_than(a->rr.f,a->test_set[0].f,N_TEST_FUNCS) ) {
          /* Use additional extrapolation value since better stats than previous*/
          a->spawn = a->rr;
          a->FOUND = TRUE;
        }
      } else {
        /* Additional extrpolation not as good, use original reflection */
        a->spawn = a->r;
        a->FOUND = TRUE;
      }
  } else //if (less_than_or_equal(a->test_set[N_PARAM-1].f,a->r.f,N_TEST_FUNCS) ) {
      if ( less_than(a->r.f,a->parent.f,N_TEST_FUNCS) ) {
        /* Solution better than parent, possibly as good as 2nd highest point */
        a->spawn = a->r;
        a->FOUND = TRUE;
      }
  }

  //bad parameter or still not found
  if (a->FOUND == FALSE) {  
    for ( int j = 0; j < N_PARAM; j++ ) a->rr.p[j] = BETA*a->parent.p[j] + (1.0-BETA)*a->pbar[j];
    a->dispatch_state = dispatch_model( a->rr.p, a->rr.f, &a->rr.soln_num );
    //a->exec_state = amoeba3;
    //case amoeba3: if (!check_model(a->dispatch_state)) return;
    if (! check_model(a->dispatch_state)) {
      retrieve_model(a->dispatch_state);
    
      //if ( less_than(a->rr.f,a->parent.f,N_TEST_FUNCS) ) { /* Contraction yielded smaller point? */
      a->spawn = a->rr;
      a->FOUND = TRUE;
    } else {
      //a->spawn = a->parent;  /* so we can consistently compare to spawn, which will be modified, while parent is untouched in case of repopulation */
      //180425LML To save the time, just use the contration, then regenerate the simplex
      //a->spawn = a->rr;
      a->FOUND = FALSE;
    }
  }

#ifdef LIUUNBLOCK
      //180425LML Note: the following block won't be execuated.

      /* Contraction did not yield smaller point, try contracting from all sides */
      //180425LML the contracting should be conducted all sides except
      //the best one. Or juest randomly
      //for ( a->i = 0; a->i < N_PARAM; a->i++ ) { /* BUG #0007: model was being (by way of conditional) not being run for (incorrect index, too, but the intention was clear) contraction from parent point, but less_than() test was still being used on uninitialized values.  Fixed loop bounds. */
      for (int i = 1; i < N_PARAM; i++) {
        /* BUG #0008: original code from AWW indicates use of test_set[ilo], but that effectively contracts about the wrong point FIXME: confirm this as correct. */
        //180425LML should be all dimensions for ( int j = /*0*/ 1; j < N_PARAM; j++ ) a->rrr.p[j] = a->r.p[j] = 0.5*(a->test_set[a->i].p[j] + a->parent/*test_set[0]*/.p[j]);
        for ( int j = 0; j < N_PARAM; j++ )
            a->rrr.p[j] = a->r.p[j] = 0.5*(a->test_set[i].p[j] + a->test_set[0].p[j]);
        a->dispatch_state = dispatch_model( a->rrr.p, a->rrr.f, &a->rrr.soln_num);
        a->exec_state = amoeba4;
case amoeba4: if (!check_model(a->dispatch_state)) return;
        retrieve_model(a->dispatch_state);

        /* IMPORTANT intentionally changing spawn not parent here and using comparison to parent, as parent is retained across repopulation!!  BUG #0009:  this used to compare to parent rather than the retained new point, thus unconditionally clobbering, exactly equivalent to running the loop backwards and short-circuiting; this does not make sense, so comparing to retained spawn now */

        if ( less_than(a->rrr.f,a->spawn.f/* yes, spawn not parent here! */,N_TEST_FUNCS) ) {
          /* Contraction from all sides yielded smaller point, store it */
          a->spawn = a->rrr;
          a->FOUND = TRUE;  /* FIXME ensure correct algorithm is to CONTINUE loop once one better point found, and whether contraction above should be using already-contracted point or independently generating single contractions (about the parent, or what?) (INDEPENDENCE and CONTINUATION (competition rather than short-circuiting) makes the points "unordered" and (independence) does not excessively weight later ones or (continuation) give earlier ones (WRT array order) a head-start,  BUT the above should be contracting using parent not [0] for this to make any sense;  changed to parent for now */
          break;    //180425LML whever find a better site, return.
        } /* Otherwise FOUND==FALSE, so we have to repopulate the simplex set */
      }
    }
  } else {
    /* Reflection yielded a lower high point */
    a->spawn = a->r;
    a->FOUND = TRUE;
  }

  a->exec_state = amoebadone;  /* keep in mind if this is ever threaded, this + FOUND may create a race within caller logic */
#endif
 } /* if */
  a->exec_state = amoebadone;
}


void *
dispatch_model( const float       *p, 
                float             *f,
                int               *soln_num )
/* NOTE: p = parameters; f = test stats; function returns Rsqr */
/***********************************************************************
  solve_model

  This subroutine checks the parameters in *p to see if they are valid.
  If so, then it starts the script that runs the model with the new
  parameter set.  Before returning to the amoeba it opens and reads 
  the R squared value for the run just completed, so that it can be 
  returned.  Invalid parameters return a default value of INVALIDMAX.

  Modifications:
  010501 Modified to record all 

***********************************************************************/
{
  DISPATCH_MODEL_STATE * state;

  state = malloc(sizeof(*state));
  state->statsfilename = malloc(4096);
  state->p = p;  /* don't need to copy this, as calling code isn't doing any fancy sequencing and can therefore just leave these arrays alone */
  state->f = f; /* or this */
  state->soln_num = soln_num;  *state->soln_num = -1;

  state->BADPARAM = FALSE;
  for(int i=0;i<N_PARAM;i++) 
    if((p[i]<param_lim[i].min) || (p[i]>param_lim[i].max))
      state->BADPARAM = TRUE;

  if(!state->BADPARAM) {  /* otherwise this is all handled on retrieve */
    char *cmdstr, *cmdstr_cur;

    state->dispatch_id = ++SOLVE_CNT; /* FIXME this is gross and is only pre-incremented so it gets the same value as is used outside of this function; it is conditionally incremented in here, so it is difficult to move the increment out of this function, but this is bad code right now */

    cmdstr_cur = cmdstr = malloc(4096);
printf("Line check 7.\n");
    sprintf(state->statsfilename,"runs/%s/%05i/stats.txt",labelstr, state->dispatch_id);
   

    /* set up command line */
    cmdstr_cur += sprintf(cmdstr_cur, "%s %s %05i", runstr, labelstr, state->dispatch_id);
    for(int i=0;i<N_PARAM;i++) {
      cmdstr_cur += sprintf(cmdstr_cur," %f",p[i]);
    }

    printf("%s\n",cmdstr);

    /* run it */
    system(cmdstr);

    free(cmdstr);
  }

  return state;
}


int check_model(DISPATCH_MODEL_STATE * state)
{
  return (state->BADPARAM) || (!access(state->statsfilename, R_OK));
}


void retrieve_model(DISPATCH_MODEL_STATE * state)
{ 
  FILE *fin;
  int i;

  if (state->BADPARAM) {
    for(i=0;i<N_TEST_FUNCS;i++) state->f[i] = (float)INVALIDMAX;  /* set stats such that they're "bad" */
    fprintf(fopti,"Invalid Parameter Set:\n");
    for(i=0;i<N_PARAM;i++)
      fprintf(fopti,"%f\t",state->p[i]);
    fprintf(fopti,"-> returning INVALIDMAX statistics\n");
  } else {
    int elapsed = 0; 
printf("Line check 8.\n");
    printf("Retrieving model run  (%05i): ", state->dispatch_id);
    fflush(stdout);

    while((fin=fopen(state->statsfilename,"r"))==NULL) {  /* TODO:  test errno or use access() in case something else is breaking this.  low-priority */
printf("Line check 9.\n");
      printf("\rRetrieving model run  (%05i): %d:%02d", state->dispatch_id, elapsed/60, elapsed%60);
      fflush(stdout);
      sleep(60);  elapsed += 60;
    }
    printf("\n");

    /*  Read in stats  */
    for ( i = 0; i < N_TEST_FUNCS; i++ )
      fscanf(fin,"%f",&state->f[i]);
    fclose(fin);

    for ( i = 0; i < N_PARAM; i++ )
      fprintf(fopti,"%f\t",state->p[i]);
    fprintf(fopti,"=\t(");
    for ( i = 0; i < N_TEST_FUNCS; i++ )
      fprintf(fopti,"\t%f",state->f[i]);
    fprintf(fopti,"\t)\t-1\t%i\n", state->dispatch_id); /* modified count param as SOLVE_CNT has likely changed */
    *state->soln_num = state->dispatch_id;
  }

  fflush(fopti);

  free(state->statsfilename);
  free(state);
}


void mocom_die(const char *format, ...) {
  va_list ap;
  va_start(ap, format);

  fprintf(stderr,"\nMOCOM: ERROR: ");
  vfprintf(stderr, format, ap);

  exit(1);
}


PARAM_RANGE *set_param_limits(const char *fname, int Nparam) {
/* Read in limits for all parameters */
  FILE *fin;
  PARAM_RANGE *param_lim;
  int     i;

  param_lim = (PARAM_RANGE *)calloc(Nparam, sizeof(PARAM_RANGE));

  if((fin=fopen(fname,"r"))==NULL) 
    mocom_die("Unable to open parameter range file.");

  for( i = 0; i < Nparam; i++ ) {
    fscanf(fin, "%s %f %f", param_lim[i].name, &param_lim[i].max, &param_lim[i].min);
    if (param_lim[i].max < param_lim[i].min)
      mocom_die("Parameter \"%s\" range is invalid: max (%f) is less than min (%f).\n", param_lim[i].name, param_lim[i].max, param_lim[i].min);
  }

  return(param_lim);

}

/* helper for rank() */
int dominates(ITEM * a, ITEM * b)
{
  int aLTb, aGTb, func;
  aLTb = aGTb = 0;
  
  for (func = 0; func < N_TEST_FUNCS ; func++) {
    if(a->f[func] < b->f[func]) aLTb++;
    if(a->f[func] > b->f[func]) aGTb++;
  }

  return ((aLTb>0) && (aGTb==0));
}
 
/*
 * Returns maximum rank value.
 */
int rank(ITEM *set, int size)
/* BUG #0005 -- replacing N_SET with size and adding size parameter so that random_start can sort this PROPERLY
 * note that this is taken into account in the code that replaces this old rank() code.
 */
/* BUG #0006 -- this has a corner case where it doesn't assign the current rank to anything, and instead goes
 * out of its way to spew meaningless information about the fact that it failed to operate correctly, so it has
 * been renamed and replaced with the above code.
 */
{
  int test, i, rank, deranked;
  int ranked = 0; /* debug only */

  rank = 1;

  for (i = 0; i < size; i++) set[i].rank = rank;
  
  do {
    deranked = 0;

    for (test = 0; test < size; test++)
      if(set[test].rank == rank)
        for (i = 0; i < size; i++)
          if ((set[i].rank == rank) && dominates(&set[i], &set[test])) {
            set[test].rank++;
            deranked++;
            break;
          }
    ranked += size - (ranked + deranked);
  } while (deranked && ++rank); /* test and increment */

  assert(ranked == size);

  return rank;
}


void calc_rank_probs(void) {
/**********************************************************************
  this routine computes the probability that each rank will produce
  offspring.  the lowest ranks have the highest probability of
  reproducing.
**********************************************************************/

  int    i, j;
  float  sum;

  /** Compute rank probabilities **/

  N_Rmax = 0;

  for ( i = 0; i < N_SET; i++ ) {
    sum = 0;
    for ( j = i; j < N_SET; j++ ) sum += set[j].rank;
    set[i].prob = (Rmax - set[i].rank + 1) / (N_SET * (Rmax + 1) - sum);
    if ( i == 0 ) prob[i] = set[i].prob;
    else prob[i] = set[i].prob + prob[i-1];
    if ( set[i].rank == Rmax ) N_Rmax++;
  }
}

void quick(ITEM *item, int count)
/**********************************************************************
        this subroutine starts the quick sort
**********************************************************************/
{
  qs(item,0,count-1);
}
 
void qs(ITEM *item, int left, int right)
/**********************************************************************
                     !!!!!WRONG SEE BELOW!!!!!
        this is the quick sort subroutine - it returns the values in
        an array from high to low.
**********************************************************************/
/**********************************************************************
BUG #0002
PN: This was originally an ascending sort.  BUG: this should be
updated to reflect all parts of the program affected by sorting
assumptions.  This is now a descending sort (for now), and is PROBABLY
stable, which has a possibly-important effect on whether every high-
rank element gets its turn being reflected as a component of the
downhill simplex algorithm.

Following is a list of things affected by the sorting code (bugs or
otherwise):

-Culling code assumes (for now) that this is descending (as per the
comment above, which until recently was incorrect).  This can be
reversed by simply removing the copying code in random_start_...()
**********************************************************************/

{
  register int i,j;
  ITEM x,y;

  i=left;
  j=right;
  x=item[(left+right)/2];
 
  do {
/* no, we meant the bit above about "[...] returns the values in an array from high to low */
/*    while(item[i].rank>x.rank && i<right) i++;
    while(x.rank>item[j].rank && j>left) j--; */      /*  To reverse direction, change: */
    while(item[i].rank<x.rank && i<right) i++;               /* THIS */
    while(x.rank<item[j].rank && j>left) j--;                /* THIS */
 
    /*if (i<=j) {                         
      y=item[i];
      item[i]=item[j];
      item[j]=y;
      i++;
      j--;
    }
*/
    if (i<=j) {
      if (item[i].rank>item[j].rank) {                         /* THIS  -- note that this condition makes the endpoints of the array stable */
        y=item[i];
        item[i]=item[j];
        item[j]=y;
      }
      i++;
      j--;
    }
  } while (i<=j);
 
  if(left<j) qs(item,left,j);
  if(i<right) qs(item,i,right);

}

#define M 714025
#define IA 1366
#define IC 150889
 
float ran2(long *idum)
/******************************************************************
  Random number generator from Numerical Recipes
******************************************************************/
{
        static long iy,ir[98];
        static int iff=0;
        int j;
 
        if (*idum < 0 || iff == 0) {
                iff=1;
                if ((*idum=(IC-(*idum)) % M) < 0) *idum = -*idum;
                for (j=1;j<=97;j++) {
                        *idum=(IA*(*idum)+IC) % M;
                        ir[j]=(*idum);
                }
                *idum=(IA*(*idum)+IC) % M;
                iy=(*idum);
        }
        j=1 + 97.0*iy/M;
        if (j > 97 || j < 1) mocom_die("RAN2: This cannot happen.");
        iy=ir[j];
        *idum=(IA*(*idum)+IC) % M;
        ir[j]=(*idum);
        return (float) iy/M;
}
 
#undef M
#undef IA
#undef IC
#undef ALPHA
#undef BETA
#undef GAMMA
#undef ITMAX
#undef INVALIDMAX

int less_than(float *x, float *y, int n) {
  /* counts items in first array that are less than corresponding items in 2nd
     array */
  //180424LML is not used by counts, but as TRUE or FALSE
  int i, cnt;
  cnt=0;
  for(i=0;i<n;i++) if(x[i]<y[i]) cnt++;
  return cnt == n; //cnt;  180424LML revised
}

int less_than_or_equal(float *x, float *y, int n) {
  /* counts items in first array that are less than OR EQUAL TO corresponding
     items in 2nd array */
  //180424LML is not used by counts, but as TRUE or FALSE
  int i, cnt;
  cnt=0;
  for(i=0;i<n;i++) if(x[i]<=y[i]) cnt++;
  return cnt == n; //cnt;  180424LML revised
}

void usage() {
  fprintf(stderr,"\nUSAGE: need arguments: <num start | start file> <num sets> <num param> <num tests> <model run script> <model run identifier> <optimization log> <parameter range file>\n");
    fprintf(stderr,"\n\tThis program uses the MOCOM-UA multi-objective optimizing scheme to\n\toptimize anything included in the optimization script that returns\n\tstatistics that can be minimized to find the 'actual' solution.\n\tThe result will be a set of simulations that define the Pareto solution\n\tset.  Given a sufficiently large population, the Pareto set should\n\tdefine the best set of solutions obtainable with the calibration\n\tscript.\n");
    fprintf(stderr,"\n\t<num start | start file> number of random simulation parameter sets\n\t\twith which to start the test population or the name of restart\n\t\tfile.\n");
    fprintf(stderr,"\t<num sets> number of simulation parameter sets in the test population.\n");
    fprintf(stderr,"\t<num param> number of parameters used by the simulation.\n");
    fprintf(stderr,"\t<num tests> number of test functions to be minimized.\n");
    fprintf(stderr,"\t<model run script> is the model run script to be used to control the\n\t\tmodel simulations.\n");
    fprintf(stderr,"\t<model run identifier> is a character string used to separate run\n\t\ttime files created by this program, from those created by other\n\t\tsimultaneous runs of this program.\n");
    fprintf(stderr,"\t<optimization log> is a log file which records the steps the\n\t\toptimizer has taken, and the final optimized results.\n");
    fprintf(stderr,"\t<parameter range file> is a test file, each line of which gives the\n\t\tparameter name, maximum and minimum values.\n");
    exit(0);
}
