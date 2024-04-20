PI = 3.14
G  = -9.81

print(PI, G)
print(PI == 3.14, G == -9.81)

G = nil
print(G, G == nil)

greet, whom = "Hi", "mom"
hello = greet .. ' ' .. whom .. '!'
print(hello, hello == "Hi mom!")

-- x = 11; y = 22; z = 33; a = 44; b = 55; c = 66; d = 77; e = 88; f = 99; g = 111;
x, y, z, a, b, c, d, e, f, g = 11, 22, 33, 44, 55, 66, 77, 88, 99, 111;

-- lol
x, y, z, a, b, c, d, e, f, g = g, f, e, d, c, b, a, z, y, x

print(x, y, z, a, b, c, d, e, f, g)

local i, ii, iii, iv, v, vi, vii, viii, ix, x = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10

print(i, ii, iii, iv, v, vi, vii, viii, ix, x)

i, ii, iii, iv, v, vi, vii, viii, ix, x = x, ix, viii, vii, vi, v, iv, iii, ii, i

print(i, ii, iii, iv, v, vi, vii, viii, ix, x)
