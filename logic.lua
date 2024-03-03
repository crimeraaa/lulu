GATE = {}

function GATE.NOT(exp)
    return not exp -- NOT is always a boolean
end

function GATE.NAND(lhs, rhs)
    return not (lhs and rhs)
end

function GATE.NOR(lhs, rhs)
    return not (lhs or rhs)
end

function GATE.AND(lhs, rhs)
    if not lhs then
        return lhs -- Always break/return lhs the moment we see it's falsy.
    else
        return rhs -- Otherwise always return rhs if lhs is truthy.
    end
end

function GATE.OR(lhs, rhs)
    -- I'm beginning to see why Bob went for a similar approach in the bytecode
    if lhs then
        return lhs -- Always return the moment we see left side is truthy.
    else
        return rhs -- Always return the right side otherwise. MIGHT be truthy.
    end
end
