// Event message compression for RTI events
// Since NS messages are about 1700 bytes, most of which are zero
// this code compresses out the zeros for events being sent over the net.
// George F. Riley, Georgia Tech, Spring 2000

#include <stdio.h>

int Compress(unsigned long* plTarget, unsigned long* plSource, int Count)
{ // Returns total size of target buffer
  // This code works only on buffers <= 0x10000 words long (about 250k bytes)
  // Target buffer must be 1 word longer than source, to allow for the
  // case where no zeros at all occur.
unsigned int *pTarget=(unsigned int*)plTarget, *pSource=(unsigned int*)plSource; //KALYAN
 int offset = 0;
 int skipping = 1;
 int targoffset = 0;
 int lth;

if(0){fflush(stdout);fflush(stderr);printf("Compress started count=%d\n",Count);fflush(stdout);}//KALYAN
 unsigned int/*KALYAN long*/ * pCW = pTarget; // Control word pointer
 // Format of control word is 16 bits of length, 16 bits of starting offset
 *pCW = 0;  // All zero CW means done
 while(offset < Count)
   {
     if (skipping)
       {
         if (pSource[offset] |= 0)
           { // Time to stop skipping
             skipping = 0;
             pCW = &pTarget[targoffset];
             *pCW = offset; // Set source offset, leave lth zero for now
             targoffset++;  // Leave room for CW
             lth = 0;       // Counts how many non-zero
           }
         else
           { // Still zero, just skip it
             offset++;
           }
       }
     if (!skipping)
       {
         // First see if we found a zero
         if (pSource[offset] == 0)
           { // Found a zero, but just use it unless two in a row
             if ((offset+1) < Count)
               {
                 if (pSource[offset+1] == 0)
                   {
                     skipping = 1; // Found two in a row, resume skipping
                     *pCW |= (lth << 16);
                     //*pCW |= (0x1L << 31); // just for debugging, mark cw
                     offset++; // And skip this one
                   }
               }
           }
       }
     if (!skipping)
       {
         pTarget[targoffset++] = pSource[offset++];
         lth++;
       }
   }
 if (!skipping)
   { // Need to update last CW
     *pCW |= (lth << 16);
     //*pCW |= (0x1L << 31); // just for debugging, mark cw
   }
if(0){fflush(stdout);fflush(stderr);printf("Compress done\n");fflush(stdout);}//KALYAN
 return(targoffset);
}

int Uncompress(unsigned long* plTarget, unsigned long* plSource, int Count)
{ // Reverse above compression
  // Target buffer MUST BE ZERO on entry!
unsigned int *pTarget=(unsigned int*)plTarget, *pSource=(unsigned int*)plSource; //KALYAN
int offset = 0;
int targoffset = 0;
int lth;
if(0){fflush(stdout);fflush(stderr);printf("Uncompress started count=%d\n",Count);fflush(stdout);}//KALYAN

  while(offset < Count)
    {
      // Get next control word
      targoffset = pSource[offset] & 0xffff;
      lth = pSource[offset] >> 16;
      offset++;
      for (int i = 0; i < lth; i++)
        { // Copy to target
          pTarget[targoffset++] = pSource[offset++];
        }
    }
if(0){fflush(stdout);fflush(stderr);printf("Uncompress done\n");fflush(stdout);}//KALYAN
  return(targoffset); // Last non-zero entry
}

#ifdef TEST_COMPRESS

#include <stdlib.h>

int main(int argc, char** argv)
{
unsigned long d[100];
unsigned long t[101];
unsigned long r[100];

int i;
int j;
int testcase = 1;

  for (i = 0; i < 100; i++) d[i] = 0;
  for (i = 0; i < 100; i++) r[i] = 0;
  if (argc > 1)
    {
      testcase = atol(argv[1]);
    }
  switch (testcase) {
  case 1 :
    for (i = 50; i < 100; i++) d[i] = i;
    break;
  case 2 :
    for (i = 20; i < 100; i+= 20)
      {
        for (j  = 0; j < 10; j++) d[i+j] = i + j;
      }
    break;
  case 3 :
    for (i = 20; i < 100; i+= 20)
      {
        for (j  = 0; j < 10; j++) d[i+j] = i + j;
      } 
    break;
  case 4 :
    for (i = 20; i < 100; i+= 20)
      {
        for (j  = 0; j < 10; j++) d[i+j] = i + j;
      }
    break;
  case 5 :
    for (i = 0; i < 100; i += 2) d[i] = i;
    break;
  case 6 :
    for (i = 0; i < 100; i++) d[i] = i+1;
    break;
  case 7 :
    for (i = 0; i < 50; i++) d[i] = i+1;
    break;
  }
  j = Compress(t, d, 100);
  printf("j %d\n", j);
  for (i = 0; i < j; i++) printf("Targ %d %08lx\n", i, t[i]);
  j = Uncompress(r, t, j);
  for (i = 0; i < 100; i++)
    {
      if (d[i] != r[i])
        {
          printf("Data mismatch offs %d, act %ld exp %ld\n",
                 i, r[i], d[i]);
          exit(1);

        }
    }
  printf("Validated ok\n");
}
#endif
