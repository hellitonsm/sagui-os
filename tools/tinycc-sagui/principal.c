/* principal.c - convencao do mlibc: o crt1 chama principal(argc, argv);
 * o tinycc define main() e nao sabe nada disso. */
int main(int argc, char **argv);

int principal(int argc, char **argv)
{
    return main(argc, argv);
}
