lastin = ""

function processing()
    if getinput() == lastin then return end
    lastin = getinput()
    print(lastin)
    printout("You've entered '" .. lastin .. "'")
end

local s = "hello from Aria pod ver." .. getversion()
print(s)
printout(s)
