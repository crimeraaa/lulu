use strict;
use warnings;

# Like C's `printf`, does not append a newline.
print("Enter your name: ");

# shorthand for `readline(STDIN)`. Does what you expect.
my $name = <STDIN>;

# Trim the trailing newline from the input as it was included.
chomp($name);

# Parentheses are optional for function calls.
print "Hi $name!\n";
