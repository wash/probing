/* Last changed Time-stamp: <2008-07-02 17:10:04 berni> */
/*
                  Ineractive Access to folding Routines

                  c Ivo L Hofacker
                  Vienna RNA package
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include "fold.h"
#include "part_func.h"
#include "fold_vars.h"
#include "utils.h"
#include "PS_dot.h"
#include "energy_const.h"
#include "read_epars.h"
#include "LPfold.h"
#include "RNAplfold_cmdl.h"

/*@unused@*/
static char rcsid[] = "$Id: RNAplfold.c,v 1.10 2008/07/02 15:34:24 ivo Exp $";

int unpaired;
PRIVATE void putout_pup(double *pup,int length, int winsize, char *name);
PRIVATE void putoutpU_G(double **pU,int length, int ulength, FILE *fp);
PRIVATE void putoutphakim_u(double **pU,int length, int ulength, FILE *fp);

/*--------------------------------------------------------------------------*/
int main(int argc, char *argv[]){
  struct        RNAplfold_args_info args_info;
  unsigned int  error = 0;
  char          fname[80], ffname[100], *c, *string, *input_string, *structure, *ParamFile, *ns_bases;
  unsigned int  input_type;
  int           i, length, l, sym, r, istty, winsize, pairdist;
  float         cutoff;
  int           tempwin, temppair, tempunpaired;
  FILE          *pUfp = NULL, *spup = NULL;
  double        **pup = NULL; /*prob of being unpaired, lengthwise*/
  int           noconv, plexoutput, simply_putout, openenergies;
  plist         *pl, *dpp = NULL;

  dangles   = 2;
  cutoff    = 0.01;
  winsize   = 70;
  pairdist  = 0;
  unpaired  = 0;
  simply_putout = plexoutput = openenergies = noconv = 0;
  tempwin = temppair = tempunpaired = 0;
  string = input_string = structure = ParamFile = ns_bases = NULL;

  /*
  #############################################
  # check the command line parameters
  #############################################
  */
  if(RNAplfold_cmdline_parser (argc, argv, &args_info) != 0) exit(1);
  /* temperature */
  if(args_info.temp_given)              temperature = args_info.temp_arg;
  /* do not take special tetra loop energies into account */
  if(args_info.noTetra_given)           tetra_loop=0;
  /* set dangle model */
  if(args_info.dangles_given)           dangles = args_info.dangles_arg;
  /* do not allow weak pairs */
  if(args_info.noLP_given)              noLonelyPairs = 1;
  /* do not allow wobble pairs (GU) */
  if(args_info.noGU_given)              noGU = 1;
  /* do not allow weak closing pairs (AU,GU) */
  if(args_info.noClosingGU_given)       no_closingGU = 1;
  /* do not convert DNA nucleotide "T" to appropriate RNA "U" */
  if(args_info.noconv_given)            noconv = 1;
  /* set energy model */
  if(args_info.energyModel_given)       energy_set = args_info.energyModel_arg;
  /* take another energy parameter set */
  if(args_info.paramFile_given)         ParamFile = strdup(args_info.paramFile_arg);
  /* Allow other pairs in addition to the usual AU,GC,and GU pairs */
  if(args_info.nsp_given)               ns_bases = strdup(args_info.nsp_arg);
  /* set the maximum base pair span */
  if(args_info.span_given)              pairdist = args_info.span_arg;
  /* set the pair probability cutoff */
  if(args_info.cutoff_given)            cutoff = args_info.cutoff_arg;
  /* set the windowsize */
  if(args_info.winsize_given)           winsize = args_info.winsize_arg;
  /* set the length of unstructured region */
  if(args_info.ulength_given)           unpaired = args_info.ulength_arg;
  /* compute opening energies */
  if(args_info.opening_energies_given)  openenergies = 1;
  /* print output on the fly */
  if(args_info.print_onthefly_given)    simply_putout = 1;
  /* turn on RNAplex output */
  if(args_info.plex_output_given)       plexoutput = 1;
    
  /* check for errorneous parameter options */
  if((pairdist < 0) || (cutoff < 0.) || (unpaired < 0) || (winsize < 0)){    
    RNAplfold_cmdline_parser_print_help();
    exit(EXIT_FAILURE);
  }

  /* free allocated memory of command line data structure */
  RNAplfold_cmdline_parser_free(&args_info);

  /*
  #############################################
  # begin initializing
  #############################################
  */
  if (ParamFile != NULL)
    read_parameter_file(ParamFile);

  if (ns_bases != NULL) {
    nonstandards = space(33);
    c=ns_bases;
    i=sym=0;
    if (*c=='-') {
      sym=1; c++;
    }
    while (*c!='\0') {
      if (*c!=',') {
        nonstandards[i++]=*c++;
        nonstandards[i++]=*c;
        if ((sym)&&(*c!=*(c-1))) {
          nonstandards[i++]=*c;
          nonstandards[i++]=*(c-1);
        }
      }
      c++;
    }
  }

  /* check parameter options again and reset to reasonable values if needed */
  if(openenergies && !unpaired) unpaired  = 31;
  if(pairdist == 0)             pairdist  = winsize;
  if(pairdist > winsize){
    fprintf(stderr, "pairdist (-L %d) should be <= winsize (-W %d);"
            "Setting pairdist=winsize\n",pairdist, winsize);
    pairdist = winsize;
  }
  if(dangles % 2){
    warn_user("using default dangles = 2");
    dangles = 2;
  }

  istty = isatty(fileno(stdout))&&isatty(fileno(stdin));

  /*
  #############################################
  # main loop: continue until end of file
  #############################################
  */
  do{
    /*
    ########################################################
    # handle user input from 'stdin'
    ########################################################
    */
    if(istty) print_tty_input_seq();

    /* extract filename from fasta header if available */
    fname[0] = '\0';
    while((input_type = get_input_line(&input_string, 0)) == VRNA_INPUT_FASTA_HEADER){
      printf(">%s\n", input_string);
      (void) sscanf(input_string, "%42s", fname);
      free(input_string);
    }

    /* break on any error, EOF or quit request */
    if(input_type & (VRNA_INPUT_QUIT | VRNA_INPUT_ERROR)){ break;}
    /* else assume a proper sequence of letters of a certain alphabet (RNA, DNA, etc.) */
    else{
      length = (int)    strlen(input_string);
      string = strdup(input_string);
      free(input_string);
    }

    structure = (char *) space((unsigned) length+1);

    if(noconv)  str_RNA2RNA(string);
    else        str_DNA2RNA(string);

    if(istty) printf("length = %d\n", length);

    /*
    ########################################################
    # done with 'stdin' handling, now init everything properly
    ########################################################
    */

    if(length > 1000000){
      if(!simply_putout)  printf("Switched to simple output mode!!!\n");
      simply_putout=1;
    }

    /* restore winsize if altered before */
    if(tempwin != 0){
      winsize = tempwin;
      tempwin = 0;
    }
    /* restore pairdist if altered before */
    if(temppair != 0){
      pairdist = temppair;
      temppair = 0;
    }
    /* restore ulength if altered before */
    if(tempunpaired != 0){
      unpaired      = tempunpaired;
      tempunpaired  = 0;
    }

    /* adjust winsize, pairdist and ulength if necessary */
    if(length < winsize){
      fprintf(stderr, "WARN: window size %d larger than sequence length %d\n", winsize, length);
      tempwin = winsize;
      winsize = length;
      if (pairdist>winsize) {          
        temppair=pairdist;
        pairdist=winsize;
      }
      if (unpaired>winsize) {
        tempunpaired=unpaired;
        unpaired=winsize;
      }
    }

    /*
    ########################################################
    # begin actual computations
    ########################################################
    */
    update_fold_params();

    if (length >= 5){
      /* construct output file names */
      char fname1[60], fname2[60], fname3[60], fname4[60], fname_t[60];

      strcpy(fname_t, (fname[0] != '\0') ? fname : "plfold");

      strcpy(fname1, fname_t);
      strcpy(fname2, fname_t);
      strcpy(fname3, fname_t);
      strcpy(fname4, fname_t);
      strcpy(ffname, fname_t);

      strcat(fname1, "_lunp");
      strcat(fname2, "_basepairs");
      strcat(fname3, "_uplex");
      strcat(fname4, "_openen");
      strcat(ffname, "_dp.ps");

      pf_scale  = -1;

      if(unpaired > 0){
        pup       =(double **)  space((length+1)*sizeof(double *));
        pup[0]    =(double *)   space(sizeof(double)); /*I only need entry 0*/
        pup[0][0] = unpaired;
      }

      pUfp = spup = NULL;
      if(simply_putout){
        spup = fopen(fname2, "w");
        pUfp = (unpaired > 0) ? fopen(fname1, "w") : NULL;

        pl = pfl_fold(string, winsize, pairdist, cutoff, pup, &dpp, pUfp,spup);

        if(pUfp != NULL)  fclose(pUfp);
        if(spup != NULL)  fclose(spup);
      }
      else{
        pl = pfl_fold(string, winsize, pairdist, cutoff, pup, &dpp, pUfp, spup);
        PS_dot_plot_turn(string, pl, ffname, pairdist);
        if (unpaired > 0){
          if(plexoutput){
            pUfp = fopen(fname3, "w");
            putoutphakim_u(pup,length, unpaired, pUfp);
            fclose(pUfp);
          }
          pUfp = fopen(openenergies ? fname4 : fname1, "w");
          putoutpU_prob(pup, length, unpaired, pUfp, openenergies);
          fclose(pUfp);
        }
      }
      free(pl);
      if(unpaired > 0){
        free(pup[0]);
        free(pup);
      }
      (void) fflush(stdout);
    }
    free(string);
    free(structure);
  } while (1);
  return 0;
}

/* additional output functions */

PRIVATE void putout_pup(double *pup,int length, int winsize, char *name) {
  int i;
  float factor;
  float tfact;
  FILE *FP;
  
  FP=fopen(name,"w");
  fprintf(FP,"&prob of being unpaired between i-%d and i\n",unpaired);
  fflush(NULL);
  for (i=unpaired; i<=length; i++) {
    factor=0.;
    if (i<winsize) {
      factor=1./(i-unpaired+1);
    }
    if (i>length-winsize+unpaired-1) {
      tfact=1./(length-i+1);
      if (tfact>factor) {
        factor=tfact;
      }
     
    }
    else {
      tfact=1./(winsize-unpaired+1);
      if (tfact>factor) {
        factor=tfact;
      }
    }
    fprintf(FP,"%d %.6f\n",i,pup[i]*factor);
  }
  fclose(FP);

}
PRIVATE void putoutpU_G(double **pU,int length, int ulength, FILE *fp) {
  /*put out unpaireds */
  int i,k;
  fprintf(fp,"#unpaired probabilities\n #i\tl=");
  for (i=1; i<=ulength; i++) {
    fprintf(fp,"%d\t", i);
  }
  fprintf(fp,"\n");
  for (k=1; k<=length; k++){
    fprintf(fp,"%d\t",k);
    for (i=1; i<=ulength; i++) {
      if (k+(i-1)<=length) fprintf(fp,"%.7g\t",pU[k+(i-1)][i]);
    }
    fprintf(fp,"\n");
    free(pU[k]);
  }
  free(pU[0]);
  free(pU);
  fflush(fp);
}
PRIVATE void putoutphakim_u(double **pU,int length, int ulength, FILE *fp) {
  /*put out Fopen in dekacalories per mol, and F(cond,open) also in dekacal*/
  int k;
  
  float RT = (temperature+K0)*GASCONST;
  float p0;
  float pdep;
  int f0;
  int fdep;
  
  fprintf(fp,"#energy necessary to unpair as well as to unpair if i-1 is unpaired also, if i+1 is unpaired also in dekacal/mol\n");
  for (k=1; k<=length; k++){
    fprintf(fp,"%d\t",k);   
    p0=pU[k][1];
    f0=(int) -RT*log(p0)/10;
    fprintf(fp,"%d\t", f0);
    if (k>1) {
      pdep=pU[k][2]/pU[k-1][1];
      fdep=(int) -RT*log(pdep)/10;
      fprintf(fp,"%d\t",fdep);


    }
    else  fprintf(fp,"-0\t");
    if (k<length) {
      pdep=pU[k+1][2]/pU[k+1][1];
      fdep=(int) -RT*log(pdep)/10;
      fprintf(fp,"%d\t",fdep);
    }
    else  fprintf(fp,"-0\t");
    fprintf(fp,"\n");
  }
 
  fflush(fp);
}

