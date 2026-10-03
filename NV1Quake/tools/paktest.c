/* paktest.c -- does the DOS file layer read gfx/pop.lmp correctly? */

#include <stdio.h>
#include <string.h>

typedef struct { char name[56]; int filepos, filelen; } dpackfile_t;

int main (void)
{
	FILE		*f;
	char		id[4];
	int		dirofs, dirlen, n, i;
	dpackfile_t	rec;
	unsigned short	check[128];
	int		got;
	unsigned long	sum;

	f = fopen ("id1\\pak1.pak", "rb");
	if (!f) { printf ("cannot open pak1\n"); return 1; }

	fread (id, 1, 4, f);
	fread (&dirofs, 1, 4, f);
	fread (&dirlen, 1, 4, f);
	printf ("id=%c%c%c%c dirofs=%d dirlen=%d\n", id[0],id[1],id[2],id[3],
		dirofs, dirlen);

	n = dirlen / (int)sizeof(dpackfile_t);
	printf ("sizeof(dpackfile_t)=%d numfiles=%d\n", (int)sizeof(dpackfile_t), n);

	fseek (f, dirofs, SEEK_SET);

	for (i = 0 ; i < n ; i++)
	{
		fread (&rec, 1, sizeof(rec), f);
		if (!strcmp (rec.name, "gfx/pop.lmp"))
		{
			printf ("found at %d len %d\n", rec.filepos, rec.filelen);

			fseek (f, rec.filepos, SEEK_SET);
			got = fread (check, 1, sizeof(check), f);
			printf ("read %d bytes\n", got);

			sum = 0;
			for (i = 0 ; i < 128 ; i++)
				sum += check[i];
			printf ("sum=%lu  [0]=%04x [1]=%04x [64]=%04x [127]=%04x\n",
				sum, check[0], check[1], check[64], check[127]);

			fclose (f);
			return 0;
		}
	}

	printf ("gfx/pop.lmp NOT FOUND in directory\n");
	fclose (f);
	return 1;
}
