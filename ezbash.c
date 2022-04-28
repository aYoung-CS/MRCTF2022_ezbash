#include <stdio.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_BUFSIZE 0x150
#define EXIT_SUCCESS 0
#define TOK_BUFSIZE 64
#define TOK_DELIM " \t\r\n\a"
int ezsh_cd(char **args);
int ezsh_ls(char **args);
int ezsh_help(char **args);
int ezsh_exit(char **args);
int ezsh_echo(char **args);
int ezsh_cat(char **args);
int ezsh_touch(char **args);
int ezsh_rm(char **args);
int ezsh_mkdir(char **args);
int ezsh_cp(char **args);
int ezsh_pwd(char **args);
int judge(char *a);

typedef enum
{
	DIR,
	FIL,
} TYPE;

struct Node
{
	TYPE type;
	char Name[0x10];
	char *content;
	struct Node *pre;
	struct Node *next;
	struct Node *parent; // as a dir
	struct Node *head;
};

typedef struct Node Node;

char *builtin_str[] = {
	"cd",
	"ls",
	"echo",
	"cat",
	"touch",
	"rm",
	"mkdir",
	"cp",
	"pwd",
	"help",
	"exit"};

Node *ROOT_DIR;
Node *cwd;
char pwd[0x50];

int (*builtin_func[])(char **) = {
	&ezsh_cd,
	&ezsh_ls,
	&ezsh_echo,
	&ezsh_cat,
	&ezsh_touch,
	&ezsh_rm,
	&ezsh_mkdir,
	&ezsh_cp,
	&ezsh_pwd,
	&ezsh_help,
	&ezsh_exit};

// false -> file have existed
// true -> allow to build
bool search_whether_exist(Node *pointer, char *name)
{
	while (pointer)
	{
		if (!strcmp(pointer->Name, name))
		{
			return false;
		}
		pointer = pointer->next;
	}
	return true;
}

Node *create_new_node()
{
	Node *new_node = (Node *)malloc(sizeof(Node));
	memset(new_node->Name, 0, 0x10);
	new_node->content = NULL;
	new_node->head = NULL;
	new_node->next = NULL;
	new_node->pre = NULL;
	new_node->parent = NULL;
	return new_node;
}

int judge(char *a)
{
	return ((*(a + 1) != 62) || (*a != 45) || *(a + 2));
}

size_t get_chunk_size(void *ptr){
	return (*(size_t*)(ptr-8)&0xfffffff0)-8;
}

int Check_Dir(Node *pointer)
{
	return pointer->type == DIR;
}

int Check_File(Node *pointer)
{
	return pointer->type == FIL;
}

int ezsh_num_builtins()
{
	return sizeof(builtin_str) / sizeof(char *);
}

bool find_node_pointer(Node **pointer, char *name)
{
	while (*pointer)
	{
		if (!strcmp((*pointer)->Name, name))
		{
			return true;
			break;
		}
		*pointer = (*pointer)->next;
	}
	return false;
}

void unlink_node(Node *pointer)
{
	if (pointer->next)
		pointer->next->pre = pointer->pre;
	if (pointer->pre)
		pointer->pre->next = pointer->next;
	else
	{ // first node
		cwd->head = pointer->next;
	}
	pointer->next = NULL;
	pointer->pre = NULL;
	free(pointer);
	pointer = NULL;
	return;
}

void insert_node(Node *cur_head, Node *new_node)
{

	while (cur_head->next)
	{
		cur_head = cur_head->next;
	}
	cur_head->next = new_node;
	new_node->pre = cur_head;
	return;
}

void copy_node_name(Node *new_file, char *name)
{
	int len = strlen(name);
	if (len > 0x10)
		len = 0x10;
	memset(new_file->Name, 0, 0x10);
	strncpy(new_file->Name, name, len);
	return;
}

void copy_node_content(Node *src_pointer, Node *new_file)
{
	int src_len = strlen(src_pointer->content);
	new_file->content = (char *)malloc(src_len * sizeof(char));
	memset(new_file->content, 0, src_len);
	strncpy(new_file->content, src_pointer->content, src_len);
	return;
}

void print_cur_dir_files()
{
	Node *cur_head = cwd->head;
	if (!cur_head)
	{
		return;
	}
	while (cur_head)
	{
		printf("%s", cur_head->Name);
		if (cur_head->type == DIR)
			printf("/");
		printf("  ");
		cur_head = cur_head->next;
	}
	printf("\n");
	return;
}

void overwite_file(Node *src_pointer, Node *tar_pointer)
{
	if (src_pointer == tar_pointer)
		return;
	if (!src_pointer->content && !tar_pointer->content)
	{
		;
	}
	else if (!src_pointer->content && tar_pointer->content)
	{
		free(tar_pointer->content);
		tar_pointer->content = NULL;
	}
	else if (src_pointer->content && tar_pointer->content)
	{   // overwrite
		int src_len = strlen(src_pointer->content);
		int tar_len = strlen(tar_pointer->content);
		if (src_len <= tar_len)
		{
			memset(tar_pointer->content, 0, tar_len);
		}
		else
		{
			tar_pointer = realloc(tar_pointer->content, (src_len+1) * sizeof(char));
			memset(tar_pointer->content, 0, (src_len+1) * sizeof(char));
		}
		strncpy(tar_pointer->content, src_pointer->content, src_len);
	}
	else if (src_pointer->content && !tar_pointer->content)
	{
		copy_node_content(src_pointer, tar_pointer);
	}
	return;
}

int ezsh_cp(char **args)
{
	int i = 0;
	Node *pointer = cwd->head;
	Node *real_cwd = cwd;
	Node *tar_pointer = real_cwd->head;
	Node *src_pointer = NULL;
	Node *new_file = NULL;
	bool find_tar_exist_or_not = false;
	char *token;
	bool find_exist_or_not = false;
	bool flag;
	const char sub_s[2] = "/";

	while (args[++i])
		;
	int tot = i - 1;
	int len = strlen(args[tot]);
	char line[len + 1];

	if (tot == 1)
	{
		fprintf(stderr, "ezbash: missing destination file operand after '%s'\n", args[1]);
		return 1;
	}

	strcpy(line, args[tot]);

	token = strtok(line, sub_s);
	while (token)
	{
		flag = false;
		if (!strcmp(token, "."))
		{
			;
		}
		else if (!strcmp(token, ".."))
		{
			cwd = cwd->parent;
		}
		else
		{
			pointer = cwd->head;
			flag = find_node_pointer(&pointer, token);
			if (tot > 2)
			{
				if (!flag)
				{
					fprintf(stderr, "ezbash: target '%s' is not a directory\n", args[tot]);
					cwd = real_cwd;
					return 1;
				}
				else if (!Check_Dir(pointer))
				{
					fprintf(stderr, "ezbash: target '%s' is not a directory\n", args[tot]);
					cwd = real_cwd;
					return 1;
				}
				else
				{
					cwd = pointer;
				}
			}
			else if (tot == 2)
			{
				if (!strcmp((line + len - strlen(token)), token))
				{
					// last -> dir->continue;
					// else -> create or overwrite
					src_pointer = real_cwd->head;
					tar_pointer = real_cwd->head;

					find_exist_or_not = find_node_pointer(&src_pointer, args[1]);
					if (!find_exist_or_not)
					{
						fprintf(stderr, "ezbash: cannot stat '%s': No such file or directory\n", args[1]);
						cwd = real_cwd;
						return 1;
					}
					if (!Check_File(src_pointer))
					{
						fprintf(stderr, "ezbash: -r not specified; omitting directory '%s'\n", args[1]);
						cwd = real_cwd;
						return 1;
					}

					find_tar_exist_or_not = find_node_pointer(&tar_pointer, token);
					if (!find_tar_exist_or_not)
					{ // not found -> create

						// create
						new_file = create_new_node();
						new_file->type = FIL;
						copy_node_name(new_file, args[2]);
						if (src_pointer->content)
						{
							copy_node_content(src_pointer, new_file);
						}
						else
						{ // no content, pass
							;
						}
						insert_node(cwd->head, new_file);
						cwd = real_cwd;
						return 1;
					}
					else
					{
						if (Check_Dir(tar_pointer))
						{ // copy into dir, skip here
							cwd = tar_pointer;
						}
						else if (Check_File(tar_pointer))
						{ // overwrite
							overwite_file(src_pointer, tar_pointer);
							cwd = real_cwd;
							return 1;
						}
					}
				}
			}
		}

		token = strtok(NULL, sub_s);
	}

	for (i = 1; i < tot; i++)
	{
		src_pointer = real_cwd->head;
		find_exist_or_not = find_node_pointer(&src_pointer, args[i]);
		if (!find_exist_or_not)
		{
			fprintf(stderr, "ezbash: cannot stat '%s': No such file or directory\n", args[i]);
			continue;
		}
		else if (!Check_File(src_pointer))
		{
			fprintf(stderr, "ezbash: cannot stat '%s': No such file or directory\n", args[i]);
			continue;
		}
		tar_pointer = cwd->head;
		find_tar_exist_or_not = find_node_pointer(&tar_pointer, args[i]);

		if (find_tar_exist_or_not)
		{ // overwrite
			overwite_file(src_pointer, tar_pointer);
			cwd = real_cwd;
			continue;
		}
		else
		{ // create a new file
			Node *cur_head = cwd->head;
			Node *cp_file = create_new_node();
			cp_file->type = FIL;
			copy_node_name(cp_file, src_pointer->Name);

			if (src_pointer->content)
			{
				copy_node_content(src_pointer, cp_file);
			}
			else
			{
				cp_file->content = NULL;
			}
			if (!cur_head)
			{ // first node in current dir
				cwd->head = cp_file;
			}
			else
			{
				insert_node(cur_head, cp_file);
			}
		}
	}

	cwd = real_cwd;
	return 1;
}

int ezsh_pwd(char **args)
{
	printf("%s\n", pwd);
	return 1;
}

int ezsh_mkdir(char **args)
{
	int i = 1;
	const char b1 = '.';
	const char b2 = '/';
	if (!args[i])
		fprintf(stderr, "ezbash: missing operand\n");
	while (args[i])
	{
		Node *cur_head = cwd->head;

		if (strchr(args[i], b1))
		{
			i++;
			continue;
		}
		if (strchr(args[i], b2))
		{
			i++;
			continue;
		}

		if (!search_whether_exist(cur_head, args[i]))
		{
			fprintf(stderr, "ezbash: cannot create directory ¡®%s¡¯: File exists\n", args[i]);
			i++;
			continue;
		}
		else
		{
			Node *new_dir = create_new_node();
			new_dir->type = DIR;
			new_dir->parent = cwd;
			copy_node_name(new_dir, args[i]);
			if (!cwd->head)
			{
				cwd->head = new_dir;
				i++;
				continue;
			}

			insert_node(cur_head, new_dir);
			i++;
		}
	}
	return 1;
}

int ezsh_rm(char **args)
{
	int i = 1;
	if (!args[i])
		fprintf(stderr, "ezbash: missing operand\n");
	while (args[i])
	{
		Node *pointer = cwd->head;
		bool find_exist_or_not = false;
		if (!strcmp(args[i], "-r"))
		{
			i++;
			if (!args[i])
			{
				i++;
				continue;
			}
			while (pointer)
			{
				if (!strcmp(pointer->Name, args[i]))
				{
					find_exist_or_not = true;
					if (!Check_Dir(pointer))
					{
						fprintf(stderr, "ezbash -r: cannot remove '%s': Is a file\n", args[i]);
						break;
					}
					memset(pointer->Name, 0, 0x10);
					pointer->parent = NULL;
					if (pointer->head)
					{
						Node *to_rm = pointer->head;
						do
						{
							if (to_rm->content)
								free(to_rm->content);
							free(to_rm);
							to_rm = to_rm->next;
						} while (to_rm);
					}
					unlink_node(pointer);
					break;
				}
				pointer = pointer->next;
			}
		}

		else
		{
			while (pointer)
			{
				if (!strcmp(pointer->Name, args[i]))
				{
					find_exist_or_not = true;
					if (!Check_File(pointer))
					{
						fprintf(stderr, "ezbash: '%s': Is a directory\n", args[i]);
						break;
					}
					memset(pointer->Name, 0, 0x10);
					if (pointer->content)
					{
						free(pointer->content);
						pointer->content = NULL;
					}
					unlink_node(pointer);
					break;
				}
				pointer = pointer->next;
			}
		}
		if (!find_exist_or_not)
		{
			fprintf(stderr, "ezbash: '%s': No such file or directory\n", args[i]);
		}
		i++;
	}
	return 1;
}

int ezsh_touch(char **args)
{
	int i = 1;
	const char b1 = '.';
	const char b2 = '/';
	if (!args[i])
		fprintf(stderr, "ezbash: missing operand\n");
	while (args[i])
	{
		if (strchr(args[i], b1))
		{
			i++;
			continue;
		}
		if (strchr(args[i], b2))
		{
			i++;
			continue;
		}

		Node *cur_head = cwd->head;
		if (!search_whether_exist(cur_head, args[i]))
		{ // have already existed -> skip
			i++;
			continue;
		}

		Node *new_file = create_new_node();
		new_file->type = FIL;
		copy_node_name(new_file, args[i]);
		if (!cwd->head)
		{
			cwd->head = new_file;
			i++;
			continue;
		}

		insert_node(cur_head, new_file);
		i++;
	}
	return 1;
}

int ezsh_cat(char **args)
{
	int i = 0;
	bool find_or_not = false;
	int len = 0;
	Node *pointer = NULL;
	while (args[++i])
		strlen(args[i]) > len ? len = strlen(args[i]) + 1 : len;
	char Name[len];

	i = 1;
	if (!args[i])
		fprintf(stderr, "ezbash: missing operand\n");
	while (args[i])
	{
		pointer = cwd->head;
		strcpy(Name, args[i]);
		while (pointer)
		{
			if (!strcmp(Name, pointer->Name))
			{
				if (pointer->content)
					printf("%s\n", pointer->content);
				find_or_not = true;
				break;
			}
			pointer = pointer->next;
		}
		if (!find_or_not)
			fprintf(stderr, "ezbash: %s: No such file or directory\n", args[i]);
		i++;
	}
	return 1;
}

int ezsh_ls(char **args)
{
	Node *pointer = cwd->head;
	Node *real_cwd = cwd;
	Node *cur_head = NULL;
	bool find_exist_or_not = false;
	int i = 0;
	char *token;
	const char sub_s[2] = "/";
	int len;
	int cnt;
	while (args[++i])
		len = strlen(args[i]) > len ? strlen(args[i]) + 1 : len;
	char line[len];
	int tot = i - 1;
	if (tot == 0)
	{
		print_cur_dir_files();
		return 1;
	}
	for (i = 1; i <= tot; i++)
	{

		len = strlen(args[i]);
		cnt = 0;
		strcpy(line, args[i]);
		token = strtok(line, sub_s);
		while (token)
		{
			pointer = cwd->head;
			if (!strcmp(token, "."))
			{
				cnt += strlen(token) + 1;
			}
			else if (!strcmp(token, ".."))
			{
				if (cwd->parent)
					cwd = cwd->parent;
				cnt += strlen(token) + 1;
			}
			else
			{
				find_exist_or_not = find_node_pointer(&pointer, token);
				if (!find_exist_or_not)
				{
					fprintf(stderr, "ezbash: cannot access '%s': No such file or directory\n", token);
					break;
				}
				else if (Check_File(pointer))
				{
					if (!strcmp((line + len - strlen(token)), token))
						printf("%s\n", pointer->Name);
					else
					{
						fprintf(stderr, "ezbash: cannot access '%s': Not a directory\n", args[i]);
					}
					if (tot > 1 && i < tot)
						printf("\n");
					break;
				}
				else if (Check_Dir(pointer))
				{
					cwd = pointer;
					cnt += strlen(token) + 1;
				}
			}
			if (!*(args[i] + cnt - 1) ||
				(*(args[i] + cnt - 1) == '/' && !*(args[i] + cnt)))
			{
				if (tot > 1)
					printf("%s:\n", args[i]);
				print_cur_dir_files();
			}
			if (tot > 1 && i < tot)
				printf("\n");

			token = strtok(NULL, sub_s);
		}
		cwd = real_cwd;
	}
	return 1;
}

int ezsh_echo(char **args)
{
	int i = 0;
	if (!args[1])
	{
		printf("\n");
		return 1;
	}
	while (args[++i]);
	int tot = i - 1;
	int tofile_or_not = judge(args[tot - 1]);
	if (tofile_or_not)
	{ // one, not to file
		for (i = 1; i < tot; i++)
		{
			printf("%s ", args[i]);
		}
		printf("%s\n", args[tot]);
		return 1;
	}
	else
	{ // write to file
		Node *pointer = cwd->head;
		char *target_filename = args[tot];
		bool find_exist_or_not = false;
		int bufsz;
		int cur_sz;

		find_exist_or_not = find_node_pointer(&pointer, target_filename);

		if (!find_exist_or_not)
		{
			fprintf(stderr, "ezbash: %s: No such file\n", target_filename);
			return 1;
		}
		else
		{
			if (!Check_File(pointer))
			{
				fprintf(stderr, "ezbash: %s: Is a directory\n", target_filename);
				return 1;
			}
			cur_sz = 0;
			if(pointer->content)
				memset(pointer->content, 0, get_chunk_size(pointer->content)); //overwrite every time *(pointer->content-8)
			for (i = 1; i < tot - 1; i++)
			{
				if (!pointer->content)
				{
					bufsz = DEFAULT_BUFSIZE;
					pointer->content = (char *)malloc(DEFAULT_BUFSIZE * sizeof(char));
					memset(pointer->content, 0, DEFAULT_BUFSIZE);
				}
				else{
					bufsz = get_chunk_size(pointer->content);
				}
				cur_sz += strlen(args[i])+2;
				while (cur_sz >= bufsz)
				{
					bufsz += DEFAULT_BUFSIZE;
				}
				if (bufsz > get_chunk_size(pointer->content))
				{
					pointer->content = (char*)realloc(pointer->content, bufsz * sizeof(char));
				}
				strncat(pointer->content, args[i], strlen(args[i]));
				if(i < tot -2)
					strncat(pointer->content, " ", 2);
			}
		}
		return 1;
	}
}

int ezsh_cd(char **args)
{
	if (args[1] == NULL)
	{
		fprintf(stderr, "ezbash: expected argument\n");
	}
	else if (args[2])
	{
		fprintf(stderr, "ezbash: too many arguments\n");
	}
	else
	{
		char *token;
		const char sub_s[2] = "/";
		token = strtok(args[1], sub_s);
		while (token)
		{
			if (!strcmp(token, "."))
			{
				;
			}
			else if (!strcmp(token, ".."))
			{
				if (cwd->parent)
				{
					int del_len = strlen(cwd->Name);
					int len = strlen(pwd);
					pwd[len - 1 - del_len] = '\0';
					cwd = cwd->parent;
				}
			}
			else
			{
				Node *pointer = cwd->head;
				if (search_whether_exist(pointer, token))
				{
					fprintf(stderr, "ezbash: %s: No such file or directory\n", token);
					return 1;
				}
				while (pointer)
				{
					if (!strcmp(pointer->Name, token))
					{
						break;
					}
					pointer = pointer->next;
				}
				if (!Check_Dir(pointer))
				{
					fprintf(stderr, "something wrong happened\n");
					return 1;
				}
				cwd = pointer;
				if (strlen(pwd) + strlen(pointer->Name) <= 0x50)
				{
					strcat(pwd, pointer->Name);
					strcat(pwd, "/");
				}
			}
			token = strtok(NULL, sub_s);
		}
	}
	return 1;
}

int ezsh_help(char **args)
{
	int i;
	printf("Welcome to ezbash\n");
	printf("Just have fun here!\n");
	printf("The following are built in:\n");

	for (i = 0; i < ezsh_num_builtins(); i++)
	{
		printf("%s\n", builtin_str[i]);
	}
	return 1;
}

int ezsh_exit(char **args)
{
	return 0;
}

int ezsh_execute(char **args)
{
	int i;
	if (args[0] == NULL)
	{
		return 1;
	}
	for (int i = 0; i < ezsh_num_builtins(); i++)
	{
		if (!strcmp(args[0], builtin_str[i]))
		{
			return (*builtin_func[i])(args);
		}
	}
	printf("%s: command not found\n", args[0]);
	return -1;
}

char **split_line(char *line)
{
	int bufsize = TOK_BUFSIZE;
	int position = 0;
	char **tokens = malloc(bufsize * sizeof(char *));
	char *token;

	if (!tokens)
	{
		fprintf(stderr, "ezbash: allocation error\n");
		exit(EXIT_FAILURE);
	}
	token = strtok(line, TOK_DELIM);
	while (token != NULL)
	{
		tokens[position] = token;
		position++;

		if (position >= bufsize)
		{
			bufsize += TOK_BUFSIZE;
			tokens = realloc(tokens, bufsize * sizeof(char *));
			if (!tokens)
			{
				fprintf(stderr, "ezbash: allocation error\n");
				exit(EXIT_FAILURE);
			}
		}

		token = strtok(NULL, TOK_DELIM);
	}

	tokens[position] = NULL;
	return tokens;
}
char *read_line()
{
	int bufsize = DEFAULT_BUFSIZE;
	int position = 0;
	char *buffer = malloc(bufsize * sizeof(char));
	int c;

	if (!buffer)
	{
		fprintf(stderr, "ezbash: allocation error\n");
		exit(EXIT_FAILURE);
	}

	while (1)
	{
		c = getchar();
		if (c == EOF || c == '\n')
		{
			buffer[position] = '\0';
			return buffer;
		}
		else
		{
			buffer[position] = c;
		}
		position++;
		if (position >= bufsize)
		{
			bufsize += DEFAULT_BUFSIZE;
			buffer = realloc(buffer, bufsize);
			if (!buffer)
			{
				fprintf(stderr, "ezbash: allocation error\n");
				exit(EXIT_FAILURE);
			}
		}
	}
}

void IO_INIT()
{
	setvbuf(stdin, 0LL, 2, 0);
	setvbuf(stdout, 0LL, 2, 0);
	setvbuf(stderr, 0LL, 2, 0);
	return;
}

void File_system_INIT()
{
	IO_INIT();
	ROOT_DIR = create_new_node();
	ROOT_DIR->type = DIR;
	cwd = ROOT_DIR;
	strncpy(ROOT_DIR->Name, "/", 1);
	strncpy(pwd, ROOT_DIR->Name, 1);
	return;
}

void loop()
{
	char *line;
	char **args;
	int status;

	do
	{
		printf("\033[33mhacker:%s$ \033[0m", pwd);
		line = read_line();
		args = split_line(line);
		status = ezsh_execute(args);

		free(line);
		free(args);
	} while (status);
}
int main()
{

	File_system_INIT();
	loop();

	return EXIT_SUCCESS;
}
