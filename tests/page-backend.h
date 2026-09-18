#ifndef RUM_TEST_PAGE_BACKEND_H
#define RUM_TEST_PAGE_BACKEND_H
void test_page_budget(int allocations); /* -1 means no injected failure. */
unsigned test_pages_owned(void);
#endif
