struct node {
	char *name;
	char *desc;
	int (*f)(int fd);
};

int walk(struct node *np, int fd);
