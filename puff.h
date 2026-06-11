/* puff.h - Copyright (C) Mark Adler, see puff.c */
#ifndef PUFF_H
#define PUFF_H

#ifndef NIL
#define NIL ((unsigned char *)0)
#endif

#ifdef __cplusplus
extern "C" {
#endif

int puff(unsigned char *dest, unsigned long *destlen, const unsigned char *source,
         unsigned long *sourcelen);

#ifdef __cplusplus
}
#endif

#endif
