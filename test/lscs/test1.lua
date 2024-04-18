lastin = ""
output = ""
created = false

function processing()
    lastin = getinput()
    output = ""
    if not created then
        if (brainstart("zephyr-7b-beta.Q3_K_M.gguf","")) then
            printout("Brain started")
        else
            printout("Can't start the brain")
        end
        created = true
    end

    if lastin ~= "" then
        printout("You've entered '" .. lastin .. "'")
        if lastin:sub(1,1) == "!" then
            if lastin:sub(-1,-1) == "\n" then
                output = lastin:sub(2,-2)
            else
                output = lastin:sub(2)
            end
            print("output = " .. output)
        else
            brainin(lastin)
            brainprefix("Anna:")
            while true do
                while not brainprocess(false) do
                    print("processing....")
                end
                if brainstate() ~= "ready" then break end
                print("generating...")
            end
            printout(brainout())
        end
    end
end

function outpin(pin)
    print(pin)
    printout("Sending data from " .. getname() .. " pin " .. pin .. ": " .. output)
    return output
end

local s = "Hello from main Aria pod ver." .. getversion() .. " named " .. getname()
print(s)
printout(s)
setiocount(0,1)
