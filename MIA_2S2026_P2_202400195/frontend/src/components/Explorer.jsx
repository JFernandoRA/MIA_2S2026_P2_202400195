import { useState, useEffect, useCallback } from 'react'
import { api, formatBytes } from '../api'
import { DiskMark, PartitionIcon, FolderIcon, FileIcon } from './Icons'

export default function Explorer({ hidden, refreshKey }) {
  const [disks, setDisks] = useState([])
  const [disk, setDisk] = useState(null)
  const [part, setPart] = useState(null)
  const [path, setPath] = useState('/')
  const [items, setItems] = useState([])
  const [file, setFile] = useState(null)
  const [selected, setSelected] = useState(null)
  const [error, setError] = useState('')

  const loadDisks = useCallback(async () => {
    try {
      const d = await api.disks()
      setDisks(d.disks)
      setError('')
      return d.disks
    } catch {
      setError('No se pudo conectar con el backend')
      return []
    }
  }, [])

  const loadFolder = useCallback(async (id, p) => {
    const r = await api.ls(id, p)
    if (!r.ok) {
      setError(r.error)
      setItems([])
      return
    }
    setError('')
    setItems(r.items)
  }, [])

  // Al ejecutar comandos en la consola se refresca lo que se esté viendo
  useEffect(() => {
    loadDisks().then((list) => {
      if (!disk) return
      const fresh = list.find((d) => d.path === disk.path)
      setDisk(fresh || null)
      if (!fresh) setPart(null)
    })
    if (part?.id && !file) loadFolder(part.id, path)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [refreshKey])

  function openPartition(p) {
    if (!p.id) return setError(`La partición ${p.name} no está montada`)
    if (p.fs === 'sin formato') return setError(`La partición ${p.name} no está formateada (use mkfs)`)
    setPart(p)
    setPath('/')
    setFile(null)
    setSelected(null)
    loadFolder(p.id, '/')
  }

  function goTo(p) {
    setPath(p)
    setFile(null)
    setSelected(null)
    loadFolder(part.id, p)
  }

  async function openItem(it) {
    const full = (path === '/' ? '' : path) + '/' + it.name
    if (it.type === 'carpeta') return goTo(full)
    const r = await api.file(part.id, full)
    if (!r.ok) return setError(r.error)
    setError('')
    setFile({ ...it, path: full, content: r.content })
  }

  function back() {
    setError('')
    if (file) return setFile(null)
    if (part && path !== '/') return goTo(path.substring(0, path.lastIndexOf('/')) || '/')
    if (part) return setPart(null)
    setDisk(null)
  }

  const crumbs = path.split('/').filter(Boolean)

  return (
    <div className="viewer" hidden={hidden}>
      <div className="viewer-head">
        <h1 className="card-title">{file ? 'Visualizador de archivos' : 'Visualizador del sistema de archivos'}</h1>
        <div className="viewer-actions">
          {disk && <button className="btn" onClick={back}>← atrás</button>}
          <button className="btn" onClick={() => { loadDisks(); if (part && !file) loadFolder(part.id, path) }}>actualizar</button>
        </div>
      </div>

      <nav className="crumbs">
        <button onClick={() => { setDisk(null); setPart(null); setFile(null); setError('') }}>discos</button>
        {disk && <><span>/</span><button onClick={() => { setPart(null); setFile(null); setError('') }}>{disk.name}</button></>}
        {part && <><span>/</span><button onClick={() => goTo('/')}>{part.name} ({part.id})</button></>}
      </nav>

      {part && (
        <div className="pathbar">
          <button onClick={() => goTo('/')}>/</button>
          {crumbs.map((c, i) => (
            <span key={i}>
              <button onClick={() => goTo('/' + crumbs.slice(0, i + 1).join('/'))}>{c}</button>
              {i < crumbs.length - 1 && '/'}
            </span>
          ))}
          {file && <span className="pathbar-file">/{file.name}</span>}
        </div>
      )}

      {error && <p className="form-error">{error}</p>}

      {!disk && (
        <>
          <p className="viewer-hint">Seleccione el disco que desea visualizar</p>
          {disks.length === 0 && !error && <p className="note">No hay discos. Cree uno con mkdisk en la consola.</p>}
          <div className="tile-grid">
            {disks.map((d) => (
              <button key={d.path} className="tile" onClick={() => { setDisk(d); setError('') }}>
                <span className="tile-icon"><DiskMark size={38} /></span>
                <span className="tile-name">{d.name}</span>
                <dl className="tile-info">
                  <dt>capacidad</dt><dd>{formatBytes(d.size)}</dd>
                  <dt>fit</dt><dd>{d.fit}</dd>
                  <dt>particiones</dt><dd>{d.partitions.length} ({d.mounted} montadas)</dd>
                  <dt>creado</dt><dd>{d.date}</dd>
                  <dt>ruta</dt><dd className="ellipsis" title={d.path}>{d.path}</dd>
                </dl>
              </button>
            ))}
          </div>
        </>
      )}

      {disk && !part && (
        <>
          <p className="viewer-hint">Seleccione la partición que desea visualizar</p>
          {disk.partitions.length === 0 && <p className="note">Este disco no tiene particiones.</p>}
          <div className="tile-grid">
            {disk.partitions.map((p) => (
              <button key={p.name + p.start} className={`tile ${p.id && p.fs !== 'sin formato' ? '' : 'tile-off'}`} onClick={() => openPartition(p)}>
                <span className="tile-icon"><PartitionIcon /></span>
                <span className="tile-name">{p.name}</span>
                <span className={`badge ${p.id ? 'badge-ok' : ''}`}>{p.id ? `montada · ${p.id}` : 'desmontada'}</span>
                <dl className="tile-info">
                  <dt>tamaño</dt><dd>{formatBytes(p.size)}</dd>
                  <dt>fit</dt><dd>{p.fit}</dd>
                  <dt>tipo</dt><dd>{p.type}</dd>
                  <dt>sistema</dt><dd>{p.fs}</dd>
                  <dt>inicio</dt><dd>byte {p.start}</dd>
                </dl>
              </button>
            ))}
          </div>
        </>
      )}

      {part && !file && (
        <div className="folder-view">
          <div>
            <p className="viewer-hint">Navegue entre carpetas o visualice archivos (doble clic para abrir)</p>
            {items.length === 0 && !error && <p className="note">Carpeta vacía.</p>}
            <div className="file-grid">
              {items.map((it) => (
                <button
                  key={it.name}
                  className={`file-item ${selected?.name === it.name ? 'is-selected' : ''}`}
                  onClick={() => setSelected(it)}
                  onDoubleClick={() => openItem(it)}
                  title={`${it.permStr}  ${it.owner}:${it.group}`}
                >
                  <span className={it.type === 'carpeta' ? 'icon-folder' : 'icon-file'}>
                    {it.type === 'carpeta' ? <FolderIcon /> : <FileIcon />}
                  </span>
                  <span className="file-name">{it.name}</span>
                  <span className="file-perm">{it.perm}</span>
                </button>
              ))}
            </div>
          </div>
          <aside className="details">
            {selected ? (
              <>
                <h2>{selected.name}</h2>
                <dl className="tile-info">
                  <dt>tipo</dt><dd>{selected.type}</dd>
                  <dt>permisos</dt><dd>{selected.perm} <span className="dim">{selected.permStr}</span></dd>
                  <dt>propietario</dt><dd>{selected.owner}</dd>
                  <dt>grupo</dt><dd>{selected.group}</dd>
                  <dt>tamaño</dt><dd>{selected.size} B</dd>
                  <dt>inodo</dt><dd>{selected.inode}</dd>
                  <dt>creado</dt><dd>{selected.created}</dd>
                  <dt>modificado</dt><dd>{selected.modified}</dd>
                </dl>
                <button className="btn" onClick={() => openItem(selected)}>{selected.type === 'carpeta' ? 'abrir carpeta' : 'ver contenido'}</button>
              </>
            ) : (
              <p className="note">Seleccione un elemento para ver su información.</p>
            )}
          </aside>
        </div>
      )}

      {file && (
        <div className="file-view">
          <div className="pane-head">
            <span>contenido del archivo · {file.size} bytes · {file.perm} · {file.owner}:{file.group}</span>
          </div>
          <pre className="editor output file-content">{file.content || '(archivo vacío)'}</pre>
        </div>
      )}
    </div>
  )
}
