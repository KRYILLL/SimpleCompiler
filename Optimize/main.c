#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "tac.h"
#include "mini.y.h"
#include "obj.h"
#include "cfg.h"
#include "constfold.h"
#include "copyprop.h"
#include "cse.h"
#include "licm.h"
#include "loopreduce.h"
#include "loopunroll.h"
#include "optlog.h"
#include "deadcode.h"

FILE *file_x, *file_s;

void error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
	exit(0);
}

void tac_list()
{
	out_str(file_x, "\n# tac list\n\n");
	
	TAC * cur;
	for(cur = tac_first; cur !=NULL; cur=cur->next)
	{
		out_str(file_x, "%p\t", cur);
		out_tac(file_x, cur);
		out_str(file_x, "\n");
	}
}

int main(int argc,   char *argv[])
{
	if(argc != 2) error("usage: %s filename\n", argv[0]);
	
	char *input = argv[1];
	if(input[strlen(input)-1]!='m') error("%s does not end with .m\n", input);

	if(freopen(input, "r", stdin)==NULL) error("open %s failed\n", input);

	char *output=strdup(input);

	output[strlen(output)-1]='x';
	if((file_x=fopen(output,"w"))==NULL) error("open %s failed\n", output);

	output[strlen(output)-1]='s';
	if((file_s=fopen(output,"w"))==NULL) error("open %s failed\n", output);

	tac_init();
	yyparse();
	optlog_reset();
	constfold_reset();
	copyprop_reset();
	cse_reset();
	licm_reset();
	loopreduce_reset();
	loopunroll_reset();
	/* iterate local optimizations to a fixpoint (guarded to avoid infinite loops) */
	for(int iter = 0; iter < 32; ++iter)
	{
		int folds = 0, copies = 0, eliminated = 0, hoisted = 0, collapsed = 0, unrolled = 0, dead = 0;
		folds = constfold_run();
		copies = copyprop_run();
		eliminated = cse_run();
		hoisted = licm_run();
		collapsed = loopreduce_run();
		//unrolled = loopunroll_run();
		dead = deadcode_run();
		if(folds == 0 && copies == 0 && eliminated == 0 && hoisted == 0 && collapsed == 0 && unrolled == 0 && dead == 0)
		{
			break;
		}
	}
	deadcode_run();
	tac_list();
	
	/* Build and print CFGs */
	CFG_ALL *cfg = cfg_build_all();
	cfg_print_all(cfg);
	tac_obj();
	cfg_free_all(cfg);

	fclose(file_s);
	fclose(file_x);

	return 0;
}
