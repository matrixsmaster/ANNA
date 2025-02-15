out = "123"

function processing()
end

function inpin(pin,msg)
    print(pin,msg)
    if pin == "pinA" then
        out = "pinA OK"
    end
end

function outpin(pin)
    print(pin,msg)
    if pin == "outA" then
        return out
    end
end

setiocount(2,3)
setionames({"pinA","pinB"},{"outA","outB","other"})
