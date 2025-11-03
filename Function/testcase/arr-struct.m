struct student
{
    int num;
    char name[10];
    int score[10];
};

struct group
{
    int num;
    char name[10];
    struct student stu[10];
};

struct class
{
    int num;
    char name[10];
    struct group grp[10];
};

main()
{
    int i,j;    
    char a,b;
    struct class cls[10];
    cls[5].num = 1;
    cls[5].grp[2].num = 2;
    cls[5].grp[2].stu[3].name[1] = 'b';
    cls[5].grp[2].stu[3].name[0] = 'a';

    if(0) { output "\n"; }

    i = cls[5].num;
    j = cls[5].grp[2].num;
    a = cls[5].grp[2].stu[3].name[0];
    b = cls[5].grp[2].stu[3].name[1];

    if(0) { output "\n"; }

    output i;
    output j;
    output a;
    output b;
    output "\n";
}
