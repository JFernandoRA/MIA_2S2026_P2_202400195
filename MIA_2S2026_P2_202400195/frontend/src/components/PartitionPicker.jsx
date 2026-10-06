import { useState, useEffect } from 'react'
import { api } from '../api'

// Selector de particiones montadas; con onlyExt3 solo muestra las EXT3
export default function PartitionPicker({ value, onChange, onlyExt3, refreshKey }) {
  const [options, setOptions] = useState([])

  useEffect(() => {
    api.disks().then((d) => {
      const list = []
      d.disks.forEach((disk) => disk.partitions.forEach((p) => {
        if (p.id && (!onlyExt3 || p.fs === 'EXT3')) list.push({ id: p.id, label: `${p.id} · ${p.name} · ${disk.name} · ${p.fs}` })
      }))
      setOptions(list)
      if (!value && list.length) onChange(list[0].id)
      if (value && !list.some((o) => o.id === value)) onChange(list[0]?.id || '')
    }).catch(() => setOptions([]))
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [refreshKey])

  if (!options.length) return <p className="note">No hay particiones {onlyExt3 ? 'EXT3 ' : ''}montadas. Use mount y mkfs -fs=3fs en la consola.</p>
  return (
    <select className="select" value={value} onChange={(e) => onChange(e.target.value)}>
      {options.map((o) => <option key={o.id} value={o.id}>{o.label}</option>)}
    </select>
  )
}
