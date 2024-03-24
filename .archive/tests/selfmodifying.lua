function range(start, stop, step)
    for i = start, stop, step do
        print(i)
    end
end
-- OP_GT will remain untouched
range(0, 2, 1)

-- FORPREP will change the OP_GT into an OP_LT
range(2, 0, -1)
