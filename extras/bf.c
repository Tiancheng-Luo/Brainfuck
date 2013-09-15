/*****************************************************************************
This is an interpreter for the "brainfuck" language.

It simply takes the original code, runlength encodes it, adds a couple
of minor instruction tweaks and saves it as an array.

It runs embarrassingly quickly.

But please do compile it with an optimising compiler; the interpretation 
loop is only a good start not perfect assembler.

    gcc -O3 -Wall -pedantic -ansi -o bf bf.c

This is, a little, better with GCC if you do this.

    gcc -O3 -o bf -Wall -fprofile-generate bf.c
    echo 80 | bf prime.b >/dev/null
    gcc -O3 -o bf -fprofile-use bf.c

*****************************************************************************/

/*
TODO:
    Strip image: Replace all "[...]" sequences after a Start or "]" with spaces
    "!" processing, if data from stdin stop reading on "!"
    Strip characters that match "//.*$"
*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

#ifndef M
typedef unsigned char cell; /* The type of the cell for the 'tape' */
#define MASK(x) x	    /* Mask for putchar & while */
#elif M == -1
typedef int cell;	    /* The type of the cell for the 'tape' */
#define MASK(x) x	    /* Mask for putchar & while */
#else
typedef int cell;	    /* The type of the cell for the 'tape' */
#define MASK(x) ((x)&M)	    /* Mask for putchar & while */
#endif

void read_image(char * imagename);
void process_image(void);
void run_image(void);

void hex_image(void);
void dump_prog(void);
void dump_c(void);

struct pgm {
    int mov;
    int op;
    int arg;
#ifdef PROFILE
    int profile;
#endif
} * prog;

unsigned char * image;
cell * mem;
int imagelen, proglen, memlen = 129*1024;

int opt_dump_prog = 0;
int opt_no_xtras = 0;

char *opcodes[] = {
    "stop", "add", "out", "in", "jz", "jnz", "6", "7",
    "set", "findz", "findm1", "arrayz", "save", "mul", "qset", "F"
};

int
main(int argc, char **argv)
{
    for(;;) {
	if (argc>1 && strcmp(argv[1], "-d") == 0) {
	    opt_dump_prog++; argc--; argv++;
	} else
	if (argc>1 && strcmp(argv[1], "-c") == 0) {
	    opt_dump_prog+=2; argc--; argv++;
	} else
	if (argc>1 && strcmp(argv[1], "-x") == 0) {
	    opt_no_xtras++; argc--; argv++;
	} else break;
    }
    if(argc < 2) {
	fprintf(stderr, "Usage: %s [-d][-x] filename\n", argv[0]);
	exit(1);
    }
    read_image(argv[1]);
    process_image();

    if (opt_dump_prog) {
	if (opt_dump_prog&1)
	    dump_prog();
	else
	    dump_c();
    } else {
	mem = calloc(memlen,sizeof*mem);
	run_image();
#ifdef PROFILE
	dump_prog();
#endif
    }

    if(image) free(image); image = 0; imagelen = 0;
    if(mem) free(mem); mem = 0; memlen = 0;
    return 0;
}

void
read_image(char * imagename)
{
    int fd = -1;
    int loaded = 0;
    struct stat st;
    if (image) {free(image); image=0; imagelen=0; }

    if (strcmp(imagename, "-") == 0)
	fd = 0;
    else
	if ((fd = open(imagename, 0)) < 0) goto error;
    if( fstat(fd, &st) < 0 ) goto error;

    if (S_ISREG(st.st_mode) && (int)st.st_size == st.st_size && st.st_size>0) {
	imagelen = st.st_size;

	if( (image = malloc(imagelen)) == 0 ) goto error;

	if( (loaded = read(fd, image, imagelen)) == imagelen ) {
	    if (fd>2) close(fd);
	    return;
	}
    } else imagelen = 0;

    for(;;)
    {
	int cc;
	if (imagelen < loaded + BUFSIZ) 
	    if ((image = realloc(image, imagelen = (loaded+BUFSIZ))) == 0)
		goto error;

	if ((cc = read(fd, image+loaded, imagelen-loaded)) < 0)
	    goto error;
	if (cc == 0) {
	    if (fd>2) close(fd);
	    imagelen = loaded;
	    image = realloc(image, loaded);
	    return;
	}
	loaded += cc;
    }

error:
    perror(imagename);
    exit(7);
}

void
process_image(void)
{
    int i, p;
    int c, lastc = ']';
    int len = 0;
    int depth = 0, maxdepth = 0;
    int * stack;

    /* First calculate the max length of the program after RLE */
    /* Second compact the array removing blank space */
    for(p=i=0; i<imagelen; i++) {
	switch(c = image[i])
	{
	    case '+': case '-':
		if (c != lastc) len++;
	    case '<': case '>':
		image[p++] = c;
		lastc = c;
		break;

	    case '.': case ',':
		lastc = c;
		len++;
		image[p++] = c;
		break;
	    case '[': case ']':
		lastc = c;
		len ++;
		if (c == '[') {
		    depth++;
		    if (depth > maxdepth) maxdepth = depth;
		    image[p++] = c;
		} else if (depth>0) { /* Ignore too many ']' */
		    depth --;
		    image[p++] = c;
		}
		break;
	}
    }
    /* All the whitespace we removed to the end of the image */
    while(p<imagelen) image[p++] = 0;

    proglen = len+1;
    prog = calloc(proglen, sizeof*prog);
    stack = calloc(maxdepth+1, sizeof*stack);

    lastc = 0;
    for(p=i=0; i<imagelen; i++) {
	switch(c = image[i])
	{
	    case '<': case '>':
		if (c != lastc) {
		    if (prog[p].op)
			p++;
		    lastc = c;
		}
		if (c == '>')
		    prog[p].mov ++;
		else
		    prog[p].mov --;
		break;
	    
	    case '+': case '-':
		if (c != lastc) {
		    int op2 = 1;
		    if (p==0 || prog[p-1].op != op2 || prog[p].mov) {
			prog[p].op = op2;
			p++;
		    }
		    lastc = c;
		}
		if (c == '+')
		    prog[p-1].arg ++;
		else
		    prog[p-1].arg --;
		if (prog[p-1].arg == 0) {
		    p--;
		    lastc = 0;
		}
		break;

	    case '.': case ',':
		lastc = c;
		prog[p++].op = 2 + (c == ',');
		break;

	    case '[': case ']':
		lastc = c;

		if (!opt_no_xtras) {
		    /* [-] or [+] -- set to zero, followed by string of +/- */
		    if (image[i] == '[' && image[i+2] == ']') {
			if (image[i+1] == '-' || image[i+1] == '+'){
			    prog[p++].op = 8;
			    i+=2;
			    while (image[i+1] == '-' || image[i+1] == '+') {
				if (image[i+1] == '+')
				    prog[p-1].arg ++;
				else
				    prog[p-1].arg --;
				i++;
			    }
			    break;
			}
		    }

		    /* [<<<<<] or [>>>>>] -- search for a zero. */
		    if (image[i] == '[' && (image[i+1] == '>' || image[i+1] == '<'))
		    {
			int v;
			for(v=2; ; v++) {
			    if (image[i+v] != image[i+1])
				break;
			}
			if (image[i+v] == ']') {
			    prog[p].op = 9;
			    prog[p].arg = v-1;
			    if (image[i+1] == '<') prog[p].arg = -prog[p].arg;
			    p++;
			    i+=v;
			    break;
			}
		    }

		    /* [-<<<<<+] or [->>>>>+] -- search for a minus one. */
		    if (image[i] == '[' && image[i+1] == '-' &&
			(image[i+2] == '>' || image[i+2] == '<'))
		    {
			int v;
			for(v=3; ; v++) {
			    if (image[i+v] != image[i+2])
				break;
			}
			if (image[i+v] == '+' && image[i+v+1] == ']') {
			    prog[p].op = 10;
			    prog[p].arg = v-2;
			    if (image[i+2] == '<') prog[p].arg = -prog[p].arg;
			    p++;
			    i+=v+1;
			    break;
			}
		    }

		    /* [-<<] [->>]  Zip along an array clearing the flag column. */
		    if (image[i] == '[' && image[i+1] == '-' &&
			(image[i+2] == '>' || image[i+2] == '<'))
		    {
			int v;
			for(v=3; ; v++) {
			    if (image[i+v] != image[i+2])
				break;
			}
			if (image[i+v] == ']') {
			    prog[p].op = 11;
			    prog[p].arg = v-2;
			    if (image[i+2] == '<') prog[p].arg = -prog[p].arg;
			    p++;
			    i+=v;
			    break;
			}
		    }

		    /* [->>>+>>>+<<<<<<]   Multiply or move loop. */
		    if (image[i] == ']' && depth > 0) {
			/* There will be just ADDs in the loop, with the
			 * total of the 'MOV' fields being zero.
			 * But allow a set too.
			 */
			int v, movsum = prog[p].mov, incby = 0;
			for(v=p-1; prog[v].op == 1 || prog[v].op == 8 ; v--) {
			    if (movsum == 0)
				incby += prog[v].arg;
			    movsum += prog[v].mov;
			}

			if (movsum == 0 && incby == -1 && prog[v].op == 4) {
			    prog[v].op = 12;
			    prog[v].arg = 0;
			    v++;
			    for(;v != p; v++)
			    {
				movsum += prog[v].mov;
				if (movsum == 0) {
				    prog[v].op = 8;
				    prog[v].arg = 0;
				} else if (prog[v].op == 1) {
				    /* Is another case for arg==1 useful?  */
				    prog[v].op = 13;
				} else
				    prog[v].op = 14;
			    }
			    depth --;
			    break;
			}
		    }
		}

		prog[p].op = 4 + (c == ']');

		if (c == '[') {
		    stack[depth] = p;
		    depth++;
		    if (depth > maxdepth) maxdepth = depth;
		} else if (depth>0) {
		    depth --;
		    prog[stack[depth]].arg = p - stack[depth];
		    prog[p].arg = stack[depth] - p;
		}
		p++;
		break;
	}
    }
    prog[p++].op = 0;
    if (p>proglen)
	fprintf(stderr, "Memory overun, conversion of program was longer than expected by %d bytes!\n", p-proglen);

    proglen = p;
    free(stack);
    free(image);
    image = 0;
    imagelen = 0;
}

void
dump_prog(void)
{
    int i;
    for(i=0; i<proglen; i++) {
#ifdef PROFILE
	if (prog[i].profile == 0) continue;
	printf("%-10d ", prog[i].profile);
#endif
	if (prog[i].mov)
	    printf("mov %d, ", prog[i].mov);
	printf("%s %d\n", opcodes[prog[i].op & 0xF], prog[i].arg);
    }
}

void
dump_c(void)
{
    int i;
    printf("#include <stdio.h>\n");
    printf("int mem[60000];\n");
    printf("int main(void){int*m=mem+1024,a;\n");
    for(i=0; i<proglen; i++) {
	if (prog[i].mov) {
	    if (prog[i].op == 1) {
		printf("*(m+=%d)+=%d;\n", prog[i].mov, prog[i].arg);
		continue;
	    } else
		printf("m+=%d;", prog[i].mov);
	}
	switch(prog[i].op) {
	case 0: printf("return %d;}\n", prog[i].arg); break;
	case 1: printf("*m+=%d;\n", prog[i].arg); break;
	case 2: printf("putchar(*m);\n"); break;
	case 3: printf("a=getchar();if(a!=EOF)*m=a;\n"); break;
	case 4: printf("while(*m){\n"); break;
	case 5: printf("}\n"); break;
	case 8: printf("*m= %d;\n", prog[i].arg); break;
	case 9: printf("while(*m)m+=%d;\n", prog[i].arg); break;
	case 10: printf("(*m)--;");
		 printf("while(*m+1)m+=%d;", prog[i].arg);
		 printf("(*m)++;\n");
		 break;
	case 11: printf("while(*m){*m-=1;m+=%d;}\n", prog[i].arg); break;
	case 12: printf("a= *m;\n"); break;
	case 13: if (prog[i].arg == 1) 
		    printf("*m+=a;\n");
		else if (prog[i].arg == -1)
		    printf("*m-=a;\n");
		else
		    printf("*m+=a*%d;\n", prog[i].arg);
		break;
	case 14: printf("if(a)*m= %d;\n", prog[i].arg); break;

	default: printf("/*%s %d*/\n", opcodes[prog[i].op & 0xF], prog[i].arg);
	}
    }
}

void
hex_output(FILE * ofd, int ch)
{
static char linebuf[80];
static char buf[20];
static int pos = 0, addr = 0;

    if( ch == EOF ) {
        if(pos)
            fprintf(ofd, "%06x:  %.66s\n", addr, linebuf);
        pos = 0;
             addr = 0;
    } else {
        if(!pos)
            memset(linebuf, ' ', sizeof(linebuf));
        sprintf(buf, "%02x", ch&0xFF);
        memcpy(linebuf+pos*3+(pos>7), buf, 2);

        if( ch > ' ' && ch <= '~' )
                linebuf[50+pos] = ch;
        else    linebuf[50+pos] = '.';
        pos = ((pos+1) & 0xF);
        if( pos == 0 ) {
	    fprintf(ofd, "%06x:  %.66s\n", addr, linebuf);
	    addr += 16;
	}
    }
}

void hex_image(void)
{
    int     i, j = 0;
    if (mem) {
	for (i = 0; i < memlen; i++)
	    if (mem[i])
		j = i + 1;
	j = ((j + 15) & ~0xF);
	for (i = 0; i < j; i++)
	    hex_output(stdout, mem[i]);
	hex_output(stdout, EOF);
    }
}

#ifndef EXTERNAL_RUNNER
#ifdef  __GNUC__
#ifndef __OPTIMIZE__
#warning The interpreter should be optimised for best performance.
#endif
#endif
#ifdef  __TINYC__
#warning Beware TCC does NOT optimise and the interpreter should be optimised for best performance.
#ifdef __BOUNDS_CHECKING_ON
#warning and it is even slower with bounds checking!
#endif
#endif

#ifndef NOPCOUNT
/* 12, 8, 16, 20
#define NOPCOUNT 0
 */
#endif

/* Interpreter.
 */

void run_image(void)
{
    struct pgm *p = prog;
    cell * m = mem+1024;
    int a = 0;
#if DEBUG
    fprintf(stderr, "p=0x%08x+%d, m=0x%08x+%d\n", prog, proglen, mem, memlen);
#endif

#ifndef __STRICT_ANSI__
#ifdef __GNUC__
    asm(".p2align 6");
#endif

#ifdef NOPCOUNT
#include "nops.h"
#endif
#endif

    p--;
    for(;;)
    {
	p++;
#ifdef PROFILE
	p->profile++;
#endif
	m += p->mov;
#if DEBUG
	fprintf(stderr, "p=%d, m=%d, *m=%d, %s %d\n",
		p-prog, m-mem, *m, opcodes[p->op & 0xF], p->arg);
#endif
	switch(p->op)
	{
	    case 0: goto break_break;

	    case 1: *m += p->arg; break;

	    case 2:
		{   unsigned char buf[4];
		    buf[0] = MASK(*m);
		    write(1,buf,1);
		}
		break;

	    case 3:
		{   unsigned char buf[4];
		    if (read(0,buf,1) == 1)
			*m = *buf;
		}
		break;

	    case 4:
		if (!MASK(*m))
		    p += p->arg;
		break;

	    case 5:
		if (MASK(*m))
		    p += p->arg;
		break;

	    /* [-] or [+] -- set to zero, plus any +/- after. */
	    case 8:
		*m = p->arg;
		break;

	    /* [<<<<<] or [>>>>>] -- search for a zero. */
	    case 9:
		while(MASK(*m)) m += p->arg;
		break;

	    /* [-<<<<<+] or [->>>>>+] -- search for a minus one. */
	    case 10:
		(*m)--;
		while(MASK(*m) != MASK((cell)-1)) m += p->arg;
		(*m)++;
		break;

	    /* [-<<] [->>]  Zip along an array clearing the flag column. */
	    case 11:
		while(m[0]) {
		    *m -= 1;
		    m += p->arg;
		}

	    /* [->>>+>>>+<<<<<<]  Save the loop value at the start. */
	    case 12:
		a = *m;
		break;

	    /* [->>>+>>>+<<<<<<]  Multiply and add. */
	    case 13:
		*m += a * p->arg;
		break;

	    /* [->>>+>>>+<<<<<<]  Test and set. */
	    case 14:
		if (a) *m = p->arg;
		break;
	}
#if DEBUG
	fprintf(stderr, "p=%d, m=%d, *m=%d\n", p-prog, m-mem, *m);
#endif
    }
    break_break:;
}
#endif