// Transaction demarcation examples: tpbegin/tpgetlev/tpsuspend/
// tpresume/tpcommit/tpabort in a client. No resource manager is
// involved, so the transaction has no participants and the commit is
// trivial on any platform.

#include <stdio.h>
#include <atmi.h>

static int fail(const char *what) {
  fprintf(stderr, "%s failed: %s (%d)\n", what, tpstrerror(tperrno),
          tperrno);
  return 1;
}

int main(void) {
  // doc:region tpbegin
  // tpbegin starts a transaction; the argument is the timeout in
  // seconds (0 selects the platform default)
  if (tpbegin(30, 0) == -1) {
    return fail("tpbegin");
  }
  // doc:end

  // doc:region tpgetlev
  // tpgetlev answers whether the caller is in transaction mode:
  // TP_IN_TRANSACTION or TP_NOT_IN_TRANSACTION
  int lev = tpgetlev();
  // doc:end
  printf("after begin: %d\n", lev);

  // doc:region tpsuspend
  // tpsuspend stashes the transaction in a TPTRANID so the thread can
  // leave transaction mode (another thread resumes it, or the same
  // thread resumes it later)
  TPTRANID tranid;
  if (tpsuspend(&tranid, 0) == -1) {
    return fail("tpsuspend");
  }
  // doc:end
  printf("after suspend: %d\n", tpgetlev());

  // doc:region tpresume
  // tpresume re-enters the stashed transaction
  if (tpresume(&tranid, 0) == -1) {
    return fail("tpresume");
  }
  // doc:end
  printf("after resume: %d\n", tpgetlev());

  // doc:region tpcommit
  // tpcommit ends the transaction with a commit; without participants
  // this is trivial, with resource managers it runs two-phase commit
  if (tpcommit(0) == -1) {
    return fail("tpcommit");
  }
  // doc:end
  printf("after commit: %d\n", tpgetlev());

  if (tpbegin(30, 0) == -1) {
    return fail("tpbegin");
  }
  // doc:region tpabort
  // tpabort rolls the transaction back
  if (tpabort(0) == -1) {
    return fail("tpabort");
  }
  // doc:end
  printf("after abort: %d\n", tpgetlev());
  return 0;
}
