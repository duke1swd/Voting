/*
 * Read in a csv file of votes
 * Spit out the rankings
 *
 * First column of csv file is all candidates
 * Each subsequent column is the ranking of a voter.
 * Voter puts most desireable candidate in row 2, next in row 3, etc.
 * Voter cannot record ties, except that voters do not have to rank
 * all candidates.  Unranked candidates are tied for last place in that
 * voters list.
 *
 * Input is stdin, output is stdout.
 */

/*


NOTE: Haven't dealt with tied rankings yet.  For now,
this produces a ranking, not all rankings.  Which ranking
depends on whatever qsort happens to do


 */

#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#define	MAX_CANDIDATES	50
#define	MAX_VOTERS	10

int verbose;
int debug;
int numeric_mode;

/*
 * Record string name of each candidate.
 */
#define	RANKING_C_WINNER	1
#define	RANKING_C_LOSER		2
#define	RANKING_T_WINNER	3
#define	RANKING_T_LOSER		4
#define	RANKING_LOSER		5
#define	RANKING_NONE		6

char *ranking_source_names[] = {
	"NULL",
	"Condorcet Winner",
	"Condorcet Loser",
	"Ranked Pairs Winner",
	"Ranked Pairs Loser",
	"No Rankings",
	"No Algorithm",
};

int num_candidates;
int next_winner;
int next_loser;
struct candidate_s {
	char *name;
	int ranking;
	int ranking_phase;
	int ranking_source;
	int wins_pair;
	int loses_pair;
	int ties;
} candidates[MAX_CANDIDATES];

int ranking_phase;
int ranking_tie;

/*
 * rankings[i][j] records rank voter i gave to candidate j.
 * If no ranking given, then it is zero.
 * Best candidate gets a 1, next is 2, etc.
 */
int num_voters;
int num_rankings[MAX_VOTERS];
int rankings[MAX_VOTERS][MAX_CANDIDATES];

/*
 * sr[i][j] contains the candidate index of the candidate
 * that voter i ranked in position j, zero indexed.
 */
char *sr[MAX_VOTERS][MAX_CANDIDATES];

/*
 * Majorities.
 * Who ranks higher than who?  One for each possible pair.
 */
int num_majorities;
struct majority_s {
	int c1;		// winner, if strength > 0.  Most of the code is normalized so c1 wins.
	int c2;		// winner, if strength < 0
	int strength;
	int locked;
	int flag;
} majorities[MAX_CANDIDATES * MAX_CANDIDATES];

char *myname;

/*
 * Copy a string into the malloc heap.
 * Return a pointer to it.
 */
static char *
dscopy(char *p)
{
	char *r;
	size_t n;

	n = strlen(p) + 1;
	r = malloc(n);

	memcpy(r, p, n);
	return r;
}

/*
 * Parse a field from a csv line.
 */
static char *
parsecsvf(char *p, char **r)
{
	int i;
	char *q;
	char lbuf[128];

	q = lbuf;
	for (i = 0; i < sizeof lbuf; i++) {
		// skip over some characters.
		while (*p == '\r' || *p == '\n')
			p++;

		// end of field, with more to come.
		if (*p == ',') {
			p++;
			break;
		}

		// end of field, no more to come
		if (*p == '\0')
			break;

		*q++ = *p++;
	}
	if (i >= sizeof lbuf) {
		fprintf(stderr, "%s: internal error: field too long\n", myname);
		exit(1);
	}

	// zero length field?
	if (q == lbuf) {
		if (!*p)
			return NULL;
		else
			return p;
	}

	*q++ = '\0';

	// copy the string into some dynamically allocated memory and put that pointer in the right place.
	*r = dscopy(lbuf);

	return p;
}

/*
 * Read the csv file on stdin, fill in the sr array
 * and the candidates array.
 */
static void
input()
{
	int i;
	char *p;
	int lineno;
	int c;
	char lbuf[128];

	lineno = 0;
	c = 0;
	
	/*
	 * initialize the output arrays to have null pointers in them
	 */
	memset(sr, '\0', sizeof sr);
	memset(candidates, '\0', sizeof candidates);
	next_winner = 0;

	/*
	 * Loop over the input file, one line at a time.
	 */
	while ((fgets(lbuf, sizeof lbuf, stdin) == lbuf)) {
		lineno++;
		if (c >= MAX_CANDIDATES) {
			fprintf(stderr, "%s input file has more than %d candidates\n",
				myname,
				MAX_CANDIDATES);
			exit(1);
		}
		if (strlen(lbuf) >= sizeof lbuf - 1) {
			fprintf(stderr, "%s: input line %d: line too long.\n",
				myname, lineno);
			exit(1);
		}
		p = parsecsvf(lbuf, &(candidates[c].name));
		if (lineno == 1 && strcasecmp(candidates[0].name, "candidates") == 0)
			continue;	// discard the header line.
		for (i = 0; i < MAX_VOTERS && p; i++)
			p = parsecsvf(p, &(sr[i][c]));
		if (p) {
			fprintf(stderr, "%s: input line %d has more than %d voters\n",
				myname,
				lineno,
				MAX_VOTERS);
			exit(1);
		}
		c++;
	}
}

/*
 * Does some basic error checking.
 * Also sets the number of candidates, voters, and rankings.
 */
static int icheck_errors;
static void
icheck1()
{
	int i;

	icheck_errors = 0;

	/*
	 * Is there a gap in the candidate list?
	 * If so, error exit.
	 * If not, set num_candidates.
	 */
	for (i = 0; i < MAX_CANDIDATES-1; i++)
		if (!candidates[i].name)
			break;
	num_candidates = i;
	next_loser = i - 1;
	for (i++; i < MAX_CANDIDATES; i++)
		if (candidates[i].name) {
			fprintf(stderr, "%s: found a blank candidate.\n",
				myname);
			icheck_errors++;
			break;
		}
}

static void
icheck2()
{
	int i, j;

	/*
	 * Is there a gap in any of the voters rankings?
	 * If so error exit.
	 * If not set num_rankings[voter]
	 */
	for (i = 0; i < MAX_VOTERS; i++) {
		for (j = 0; j < MAX_CANDIDATES-1; j++)
			if (!sr[i][j])
				break;
		num_rankings[i] = j;
		for (j++; j < MAX_CANDIDATES; j++) 
			if (sr[i][j]) {
				fprintf(stderr, "%s: gap in voter %d rankings\n",
					myname, i);
				icheck_errors++;
			}
	}

	for (i = 0; i < MAX_VOTERS - 1; i++)
		if (num_rankings[i] == 0)
			break;
	num_voters = i;
	for (i++; i < MAX_VOTERS; i++)
		if (num_rankings[i]) {
			fprintf(stderr, "%s: found a blank voter column\n",
				myname);
			icheck_errors++;
		}

	if (icheck_errors) {
		fprintf(stderr, "%s: exiting on il-formed matrix\n",
			myname);
		exit(1);
	}
} 

static void
ncheck2()
{
	int i, j, t;
	char tbuf[128];

	/*
	 * Did any voter give a rank < 1 or > number of candidates?
	 * Or a rank that isn't an integer?
	 */
	memset(num_rankings, '\0', sizeof num_rankings);
	for (i = 0; i < MAX_VOTERS; i++) {
		num_rankings[i] = num_candidates;
		for (j = 0; j < MAX_CANDIDATES-1; j++) 
			if (sr[i][j]) {
				t = atoi(sr[i][j]);
				sprintf(tbuf, "%d", t);
				if (strcmp(tbuf, sr[i][j]) != 0) {
					fprintf(stderr, "%s: voter %i row %d is not an integer (%s)\n",
							myname, i, j, sr[i][j]);
					icheck_errors++;
				}
				if (t < 1 || t > num_candidates) {
					fprintf(stderr, "%s: votor %i gave rank %d to candidate %d outside range [1-%d]\n",
							myname, i, t, j, num_candidates);
					icheck_errors++;
				}

				if (j >= num_candidates) {
					fprintf(stderr, "%s: votor %i gave a rank to an unknown candidate (%d)\n",
							myname, i, j);
					icheck_errors++;
				}
			}

	}
}

static void
icheck3()
{
	int i;

	for (i = 0; i < MAX_VOTERS - 1; i++)
		if (num_rankings[i] == 0)
			break;
	num_voters = i;
	for (i++; i < MAX_VOTERS; i++)
		if (num_rankings[i]) {
			fprintf(stderr, "%s: found a blank voter column\n",
				myname);
			icheck_errors++;
		}

	if (icheck_errors) {
		fprintf(stderr, "%s: exiting on il-formed matrix\n",
			myname);
		exit(1);
	}
} 

/*
 * Debugging routine.
 */
static void
print_num_rankings()
{
	int i;

	printf("Num Rankings:\n");
	for (i = 0; i < num_voters; i++)
		printf("\tv %2d: %d\n", i, num_rankings[i]);
}

static void
print_sr_array()
{
	int v, i;

	printf("Input data:\n");
	for (v = 0; v < num_voters; v++) {
		printf("\tv %2d: ", v);
		for (i = 0; i < num_rankings[v]; i++) {
			if (i)
				printf(", ");
			printf("%s", sr[v][i]);
		}
		printf("\n");
	}
}

/*
 * Converts the string matrix into the integer matrix.
 */
static void
iconv()
{
	int errors;
	int i, j, k;
	int gave_ranking[MAX_CANDIDATES];

	errors = 0;
	memset(rankings, '\0', sizeof rankings);

	for (i = 0; i < num_voters; i++) {
		memset(gave_ranking, 0, sizeof gave_ranking);
		for (j = 0; j < num_rankings[i]; j++) {
			for (k = 0; k < num_candidates; k++)
				if (strcasecmp(sr[i][j], candidates[k].name) == 0) {
					if (gave_ranking[k] == 1) {
						fprintf(stderr, "%s: voter %d ranked candidate %s more than once.\n",
							myname, i, candidates[k].name);
						errors++;
					}
					gave_ranking[k]++;
					rankings[i][k] = j + 1;
					break;
				}
			if (k >= num_candidates) {
				fprintf(stderr, "%s: voter %d ranked non-existant candidate %s\n",
					myname,
					i + 1,
					sr[i][j]);
				errors++;
			}
		}
	}
	if (errors)
		exit(1);
}

/*
 * Converts the string matrix into the integer matrix.
 * This version assumes the string matrix contains rankings, rather than names
 */
static void
nconv()
{
	int i, j;

	memset(rankings, '\0', sizeof rankings);

	for (i = 0; i < num_voters; i++) 
		for (j = 0; j < num_rankings[i]; j++)
			if (sr[i][j])
				rankings[i][j] = atoi(sr[i][j]);
}

/*
 * Debugging routine.
 */
static void
print_ranking_array()
{
	int v;
	int r;

	printf("Rankings by Voter\n");
	for (v = 0; v < num_voters; v++) {
		printf("\tv %2d: ", v);
		for (r = 0; r < num_candidates; r++) {
			if (r)
				printf(", ");
			printf("%d", rankings[v][r]);
		}
		printf("\n");
	}
	printf("\n");
}

/*
 * find out who is prefered to who by how much.
 */
static void
create_majorities()
{
	int i, j;
	int t;
	int r1, r2;
	struct majority_s *mp;

	// init the array.
	memset(majorities, 0, sizeof majorities);
	mp = majorities;
	for (i = 0; i < num_candidates - 1; i++)
		for (j = i + 1; j < num_candidates; j++) {
			mp->c1 = i;
			mp->c2 = j;
			mp++;
		}

	/*
	 * Loop over all majorities, then all votors.
	 * For this majority record this voter's preference.
	 */
	num_majorities = num_candidates * (num_candidates - 1) / 2;
	for (i = 0, mp = majorities; i < num_majorities; i++, mp++)
		for (j = 0; j < num_voters; j++) {
			r1 = rankings[j][mp->c1];
			r2 = rankings[j][mp->c2];
			if (r1 && r2) {
				if (r1 < r2)
					mp->strength++;
				else if (r1 > r2)
					mp->strength--;
			} else if (r1)
				mp->strength++;
			else if (r2)
				mp->strength--;
		}
	
	/*
	 * Loop over all majorities.
	 * Count the number of tie votes.
	 * Normalize the majorities so c1 always wins.
	 */
	j = 0;
	for (i = 0, mp = majorities; i < num_majorities; i++, mp++)
		if (mp->strength == 0)
			j++;
		else if (mp->strength < 0) {
			t = mp->c1;
			mp->c1 = mp->c2;
			mp->c2 = t;
			mp->strength = -mp->strength;
		}
	if (j && verbose)
		printf("Warning: %d ties were found.\n", j);
}

/* DEBUGGING ROUTINE */
static void
check_majorities(char *s)
{
	int i;
	struct majority_s *mp;

	fprintf(stderr, "checking majorities: %s\n", s);
	for (i = 0, mp = majorities; i < num_majorities; i++, mp++)
		if (mp->c1 == mp->c2) {
			fprintf(stderr, "majority %d is bad (%d)\n",
				i, mp->c1);
			exit(1);
		}
}

/* 
 * return 0 if cannot figure out two majorities
 * return -1 if first majority is more important than second.
 * return 1 if second majority is more important than first
 *
 * Record if we ever returned 0.
 */
static int
compar(const void *p, const void *q)
{
	int i;
	int lp, lq;
	const struct majority_s *mp = p;
	const struct majority_s *mq = q;
	struct majority_s *mr;

	if (mp->strength > mq->strength)
		return -1;
	if (mp->strength < mq->strength)
		return 1;

	/*
	 * Tie.  Look to race between losers.
	 */
	lp = mp->c2;
	lq = mq->c2;
	
	/*
	 * Call it a tie if both won against the same loser
	 */
	if (lp == lq) {
		ranking_tie = 1;
		return 0;
	}
	
	/*
	 * Find the race that was between the losers.
	 */
	for (i = 0, mr = majorities; i < num_majorities; i++, mr++)
		if ((mr->c1 == lp && mr->c2 == lq) ||
		    (mr->c2 == lp && mr->c1 == lq))
		     	break;
	if (i == num_majorities) {
		fprintf(stderr, "%s: internal error in compar.\n", myname);
		fprintf(stderr, "\tlp = %d, lq = %d\n", lp, lq);
		exit(1);
	}

	/*
	 * If the losers tied, then we are tied.
	 */
	if (mr->strength == 0) {
		ranking_tie = 1;
		return 0;
	}

	/*
	 * P wins if Q's loser beats P's loser.
	 */
	if (lq == mr->c1)
		return -1;
	return 1;
}

static int
count_tied_majorities()
{
	int i, j;
	int count;

	count = 0;
	for (i = 0; i < num_majorities - 1; i++)
		for (j = i + 1; j < num_majorities; j++)
			if (compar(majorities + i, majorities + j) == 0)
				count++;
	return count;
}

static void
do_sort()
{
	qsort(majorities, num_majorities, sizeof majorities[0], compar);
}

static int
remove_m(const void *p, const void *q)
{
	const struct majority_s *mp = p;
	const struct majority_s *mq = q;

	return mp->flag - mq->flag;
}

static void
remove_pairings(struct candidate_s *cp)
{
	int i, c;
	int n;
	struct majority_s *mp;

	c = cp - candidates;

	n = 0;
	for (i = 0, mp = majorities; i < num_majorities; i++, mp++)
		if (mp->c1 == c || mp->c2 == c) {
			mp->flag = 1;
			n++;
		} else
			mp->flag = 0;

	// sort the dead ones to the end.
	qsort(majorities, num_majorities, sizeof majorities[0], remove_m);
	num_majorities -= n;
}

static void
pull_unranked_losers()
{
	int i, j;
	int count;
	struct candidate_s *cp;

	count = 0;
	// loop over all canidates not already ranked.
	for (i = 0, cp = candidates; i < num_candidates; i++, cp++)
		if (!cp->ranking_source) {
			// look to see if any voter gave this candidate a rank.
			for (j = 0; j < num_voters; j++)
				if (rankings[j][i])
					break;
			if (j >= num_voters) {
				// candidate was unranked
				cp->ranking_source = RANKING_LOSER;
				cp->ranking_phase = ranking_phase;
				count++;
			}
		}
	
	// loop over all candidates give the RANKING_LOSER status
	if (count) {
		next_loser -= count - 1;
		for (i = 0, cp = candidates; i < num_candidates; i++, cp++)
			if (cp->ranking_source == RANKING_LOSER) {
				cp->ranking = next_loser;
				remove_pairings(cp);
			}
		next_loser--;
		ranking_phase++;
	}
}

/*
 * If any of the candidates are Condorcet winners, because they beat all others,
 * or Condorcet losers, because they are beat by all others, then
 * we know their rankings.
 *
 * returns true if any candidates were found and pulled out.
 */
static int
pull_condorcet()
{
	int i;
	int count;
	struct majority_s *mp;
	struct candidate_s *cp, *wp, *lp;
	
	for (i = 0, cp = candidates; i < num_candidates; i++, cp++) {
		cp->wins_pair = 0;
		cp->loses_pair = 0;
		cp->ties = 0;
	}

	for (i = 0, mp = majorities; i < num_majorities; i++, mp++)
		if (mp->strength) {
			candidates[mp->c1].wins_pair = 1;
			candidates[mp->c2].loses_pair = 1;
		} else {
			candidates[mp->c1].ties = 1;
			candidates[mp->c2].ties = 1;
		}
			

	// how many only win, never lose?
	count = 0;
	wp = NULL;
	for (i = 0, cp = candidates; i < num_candidates; i++, cp++)
		if (cp->wins_pair && !cp->loses_pair && !cp->ties) {
			count++;
			wp = cp;
		}

	// If we have exactly one winner, they are the Condorcet winner.
	if (count == 1) {
		wp->ranking = next_winner++;
		wp->ranking_source = RANKING_C_WINNER;
		wp->ranking_phase = ranking_phase;
	} else
		wp = NULL;

	// how many only lose, never win??
	count = 0;
	lp = NULL;
	for (i = 0, cp = candidates; i < num_candidates; i++, cp++)
		if (!cp->wins_pair && cp->loses_pair && cp->ties) {
			count++;
			lp = cp;
		}

	// If we have exactly one loser, they are the Condorcet loser.
	if (count == 1) {
		lp->ranking = next_loser--;
		lp->ranking_source = RANKING_C_LOSER;
		lp->ranking_phase = ranking_phase;
	} else
		lp = NULL;

	count = 0;
	if (wp) {
		remove_pairings(wp);
		count++;
	}
	if (lp) {
		remove_pairings(lp);
		count++;
	}
	if (wp || lp)
		ranking_phase++;
	return count;
}

/*
 * Is there a path from c2 to c1?
 *
 * An arc goes from x to y if these exists i such that
 *  majorities[i].locked
 *  majorities[i].c1==x
 *  majorities[i].c2==y
 */
static int
path_to(int c1, int c2)
{
	int i;
	struct majority_s *mp;

	// is there a direct arc from c2 to c1?
	for (i = 0, mp = majorities; i < num_majorities; i++, mp++)
		if (mp->locked && mp->c1 == c2)
			// is there an arc?
			if (mp->c2 == c1)
				return 1;
	
	// is there a path from c2 to c1?
	for (i = 0, mp = majorities; i < num_majorities; i++, mp++)
		if (mp->locked && mp->c1 == c2) 
			// there is an arc from c2 to mp->c2.
			// is there a path from mp->c2 to c1?
			if (path_to(c1, mp->c2))
				return 1;
	return 0;
}

/*
 * This routine "locks" all pairings that can be locked.
 * Pairings are locked in turn, provided they do not create
 * a cycle in the graph.
 */
static void
do_lock()
{
	int i;
	int not_locked;
	struct majority_s *mp;

	not_locked = 0;
	for (i = 0, mp = majorities; i < num_majorities; i++, mp++) {
		if (!path_to(mp->c1, mp->c2))
			mp->locked++;
		else
			not_locked++;
	}
	if (verbose)
		printf("%d pairings were locked, %d were not locked.\n",
			num_majorities - not_locked, not_locked);
}

/*
 * Find all the winners by the ranked pairs method.
 */
static void
find_rp_winners()
{
	int i;
	int count;
	struct majority_s *mp;
	struct candidate_s *cp;
	int is_not_winner[MAX_CANDIDATES];
	int mentioned[MAX_CANDIDATES];

	memset(is_not_winner, 0, sizeof is_not_winner);
	memset(mentioned, 0, sizeof is_not_winner);

	for (i = 0, mp = majorities; i < num_majorities; i++, mp++) {
		mentioned[mp->c1] = 1;
		mentioned[mp->c2] = 1;
		if (mp->locked)
			is_not_winner[mp->c2] = 1;
	}

	count = 0;
	for (i = 0, cp = candidates; i < num_candidates; i++, cp++)
		if (mentioned[i] && !is_not_winner[i]) {
			count++;
			cp->ranking = next_winner;
			cp->ranking_source = RANKING_T_WINNER;
			cp->ranking_phase = ranking_phase;
			remove_pairings(cp);
		}
	next_winner += count;
	if (verbose)
		printf("Ranked pairs yielded %d winners at phase %d\n", count, ranking_phase+1);
	if (count)
		ranking_phase++;
}

/*
 * Sort the candidates and print them out.
 */
static int
rank_order(const void *p, const void *q)
{
	const struct candidate_s *cp = p;
	const struct candidate_s *cq = q;

	return cp->ranking - cq->ranking;
}

static void
print_rankings()
{
	int i;
	int count;
	int source;
	int ranking_tie_phase;
	char flag;
	struct candidate_s *cp;

	source = RANKING_NONE;
	count = 0;
	for (i = 0, cp = candidates; i < num_candidates; i++, cp++) {
		if (ranking_tie_phase < 0 && cp->ranking_source == RANKING_T_WINNER)
			ranking_tie_phase = cp->ranking_phase;
		if (!cp->ranking_source)
			count++;
	}
	if (count == 1)
		source = RANKING_T_LOSER;

	printf("\n Name     Rank Phase Ranking Source\n");

	// all candidates not yet ranked are tied for middle.
	for (i = 0, cp = candidates; i < num_candidates; i++, cp++) {
		if (!cp->ranking_source) {
			cp->ranking_source = source;
			cp->ranking = next_winner;
			cp->ranking_phase = ranking_phase;
		}
	}

	qsort(candidates, num_candidates, sizeof candidates[0], rank_order);

	ranking_tie_phase = -1;
	if (ranking_tie)
		for (i = 0, cp = candidates; i < num_candidates; i++, cp++)
			if (cp->ranking_source == RANKING_T_WINNER) {
				ranking_tie_phase = cp->ranking_phase;
				break;
			}

	for (i = 0, cp = candidates; i < num_candidates; i++, cp++) {
		flag = ' ';
		if (ranking_tie_phase >= 0 && cp->ranking_phase >= ranking_tie_phase)
			flag = '*';
		printf("%10s %3d%c %4d  %s\n",
			cp->name,
			cp->ranking + 1,
			flag,
			cp->ranking_phase + 1,
			ranking_source_names[cp->ranking_source]);
	}
}

/*
 * Debugging routine.
 */
static void
print_majorities()
{
	int i;
	struct majority_s *mp;

	printf("Majorities\n");
	for (i = 0, mp = majorities; i < num_majorities; i++, mp++)
		printf("\t%10s > %10s strength %3d\n",
			candidates[mp->c1].name,
			candidates[mp->c2].name,
			mp->strength);
	printf("\n");
}

static void
set_defaults() {
	debug = 0;
	verbose = 0;
	numeric_mode = 0;
}

static void
usage()
{
	set_defaults();
	fprintf(stderr, "Usage: %s [options] <input\n", myname);
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "\t-v <verbose mode>\n");
	fprintf(stderr, "\t-d <debugging>\n");
	fprintf(stderr, "\t-n <numeric input mode.  See long help.>\n");
	fprintf(stderr, "\t-h <print long help and exit>\n");
	exit(1);
}

/*
 * Long help.
 * Describes input file format and exits
 */
static void
long_help()
{
	char *msg = 
	"\n"
	"    The input file is a comma-separated-values data file (.csv)\n"
	"    The first line is a header.  The first value in the header line\n"
	"    should be \"candidates\".  Each subsequent value in the header\n"
	"    is the name or ID of a voter.  The header line is ignored.\n"
	"\n"
	"    In normal mode, the first column in the rest of the\n"
	"    file is a list of the candidates, in no particular order.\n"
	"    Each column after that contains the candidates in the order\n"
	"    ranked by that voter.  The voter need not rank all candidates.\n"
	"    Any candidate not ranked is tied for last place by that voter.\n"
	"    There is no way to represent ties.\n"
	"\n"
	"    In numeric mode (-n), the first column in the rest of the\n"
	"    file is a list of the candidates, in no particular order.\n"
	"    The rest of the columns contain integers.  The integer in\n"
	"    column X row Y is the rank given by voter in column X to the\n"
	"    candidate in column Y.	Ties, gaps, etc are possible.\n";

	fprintf(stderr, "%s: Long help:\n", myname);
	fputs(msg, stderr);
	exit(1);
}

static void
grok_args(int argc, char **argv)
{
	int c;
	int errors;
	int nargs;

	myname = *argv;

	set_defaults();
	errors = 0;

	while ((c = getopt(argc, argv, "vhdn")) != EOF)
		switch(c) {
			case 'v':
				verbose++;
				break;
			case 'd':
				debug++;
				break;
			case 'n':
				numeric_mode++;
				break;
			case 'h':
				long_help();
				break;
			case '?':
			default:
				usage();
		}

	nargs = argc - optind;
	if (nargs > 0) {
		fprintf(stderr, "%s: no positional arguments\n", myname);
		errors++;
	}

	if (errors)
		usage();
}

int
main(int argc, char **argv)
{
	grok_args(argc, argv);
	input();
	if (debug)
		print_sr_array();
	icheck1();
	if (numeric_mode)
		ncheck2();
	else
		icheck2();
	icheck3();
	if (debug)
		print_num_rankings();

	if (verbose)
		printf("%d candidates and %d voters found.\n",
			num_candidates, num_voters);

	if (numeric_mode)
		nconv();
	else
		iconv();

	if (debug)
		print_ranking_array();

	create_majorities();
	if (debug) {
		check_majorities("after creating them");
		if (debug > 1)
			print_majorities();
	}
	ranking_phase = 0;
	pull_unranked_losers();
	while (pull_condorcet())
		;
	printf("%d majorities and %d majority pairings remain.  %d majority ties were found.\n",
		num_majorities,
		num_majorities * (num_majorities - 1) / 2,
		count_tied_majorities());
	ranking_tie = 0;
	while (num_majorities) {
		do_sort();
		do_lock();
		find_rp_winners();
	}
	if (ranking_tie)
		printf("Ranking ties were found.  RP ranking is not unique.\n");
	print_rankings();
}
