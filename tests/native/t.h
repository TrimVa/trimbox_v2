// Mini-cadre de test : aucune dépendance, sortie lisible dans les journaux CI.
#pragma once
#include <stdio.h>
#include <math.h>
static int g_fail = 0, g_pass = 0;
#define CHECK(c, ...) do{ if(c){ g_pass++; } else { g_fail++; printf("  ECHEC %s:%d : ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } }while(0)
#define NEAR(a, b, tol, what) CHECK(fabs((double)(a)-(double)(b)) <= (tol), "%s : %.6f au lieu de %.6f (tol %.6f)", what, (double)(a), (double)(b), (double)(tol))
#define DONE(name) do{ printf("%-12s %d OK, %d ECHEC(S)\n", name, g_pass, g_fail); return g_fail ? 1 : 0; }while(0)
