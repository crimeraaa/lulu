use strict;
use warnings;

my $x = "4T";
my $y = 3;

# `.` is the Perl string concatenation operator. Like in Lua, this automatically
# converts numbers to strings for the operation.
# 
# ALSO: Escape sequences are only valid in double-quoted strings.
# For single-quoted ones they are treated literally.
print $x . $y . "\n";

# And just like in Lua, strings are coerced to numbers for arithmetic.
# However unlike Lua, if Perl found `use warnings;` it will warn you of bad
# conversions # `.` is the Perl string concatenation operator.like `"4T"`.
# print $x + $y;

# Although there is no dedicated `tonumber()` function as in Lua, you can test
# it using the `looks_like_number` function from the `Scalar::Util` module.
#
# `qw` means "quote word". For the arguments between the parentheses, it creates
# an array of strings, each separated by whitespace. It extracts elements before
# and after the whitespace.
#
# We need this in order to ONLY import `looks_like_number()` and nothing else,
# as otherwise the `use` keyword will import everything from the module.
# If you'd like to import other things, simply specify them in a whitespace
# separated list like `qw(looks_like_number something_else cool_thing)`.
use Scalar::Util qw(looks_like_number);

if (looks_like_number($x) && looks_like_number($y)) {
    print $x + $x;
    print "\n";
} else {
    print "Nope!\n";
}
