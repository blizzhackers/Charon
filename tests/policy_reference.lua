-- Independent scalar oracle for Lua-only fixtures, never loaded by production.
-- The native CTest runs the same fixtures using the actual C++ binding instead.
if policy then return policy end
local reference = {}
function reference.compile(weights)
  local model={b2=weights.b2}
  for _,name in ipairs({'w1','b1','w2'}) do
    model[name]={}
    for i,v in ipairs(weights[name]) do model[name][i]=v end
  end
  return model
end
function reference.rank(model, rows)
  local best,scores=1,{}
  for a,x in ipairs(rows) do
    local score=model.b2
    local hidden=#model.b1
    local inputs=#model.w1/hidden
    assert(#x==inputs,'each candidate needs one value per model input')
    for h=1,hidden do
      local z=model.b1[h]
      for i=1,inputs do z=z+model.w1[(h-1)*inputs+i]*x[i] end
      score=score+model.w2[h]*math.max(0,z)
    end
    scores[a]=score
    if score>scores[best] then best=a end
  end
  return best,scores
end
policy=reference
return reference
