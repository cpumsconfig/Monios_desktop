/*
 * tinyhello.c - input for the MoniOS in-kernel tiny toolchain.
 *
 * Exercises:
 *   - object-like macro expansion (#define MSG ...)
 *   - ordered puts() / printf() / putchar() output calls
 *   - integer return
 *
 * Build inside MoniOS (one-shot driver, no host tools):
 *   gcc C:\Monios\Apps\mingw\tinyhello.c -o C:\Monios\Apps\mingw\tinyhello.exe
 *   run C:\Monios\Apps\mingw\tinyhello.exe
 */
#define HELLO  "Hello from the in-kernel gcc 0.3!"
#define DIV    "========================\r\n"

int main(void)
{
    puts(DIV);
    puts(HELLO);
    printf("One-shot C -> PE .exe works.\r\n");
    putchar('>');
    putchar('\n');
    return 0;
}
