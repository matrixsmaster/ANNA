sock = "/tmp/test"
sock_out = "/tmp/test_result"
msg = ""

function now()
    return "[".. tostring(millis()).. "] "
end

function processing()
    if fmecheck(sock) then
        print("Socket exists!")
        msg = fmereceive(sock,256)
        print(now().. msg)
        printout(msg)
    end

    if msg ~= "" then
        local t = "I have the following message:\n".. msg.. " @ ".. now()
        if fmesend(sock_out,t,10) then
            print("Sent")
        else
            print("Not sent")
        end
    end
end

function outpin(pin)
    local t = msg
    msg = ""
    return t
end

setiocount(0,1)
print(getname().. " ".. now())
