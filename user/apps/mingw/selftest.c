/*
 * Input for MoniOS tiny gcc. The first stage supports ordered output calls.
 */
int main(void)
{
    puts("Hello from self-written gcc/as/ld!");
    printf("Multiple output calls work.\r\n");
    putchar('>');
    putchar('\n');
    return 0;
}
