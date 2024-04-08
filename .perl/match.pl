#!/usr/bin/env perl

use warnings;
use strict;

my $sample = "/* this is a C comment! */";

print "Before: \'$sample\'\n";

# See: https://perldoc.perl.org/perlpod
# Formatting codes: https://perldoc.perl.org/perlpod#Formatting-Codes
=pod

=head1 REGEX BREAKDOWN:

Using C<!> for delimiters to avoid needing to escape C</> a lot.

=over

=item /\*\s+

Consume the opening C</*> of a C-style comment and any number of whitespaces.

=item (.*?)

Capture the comment's contents, store it in the variable $1, until...

=item \s+\*/

We find a terminating C<*/> preceded by any number of whitespaces.

=back

=cut
$sample =~ s!/\*\s+(.*?)\s+\*/!$1!g;
print "After: \'$sample\'\n";
