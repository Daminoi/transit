# Compito Transit

Il programma simula l'attraversamento di una stazione da parte di treni; i treni richiedono un tempo fisso per attraversare la stazione e si contendono il numero limitato di binari.

Il programma richiede 4 parametri e ha 1 facoltativo (in ordine):
  - N numero di binari (intero maggiore di 0)
  - T tempo di transito per ogni treno in ms (intero maggiore di 0)
  - Tmin tempo in ms minimo prima che un nuovo treno possa arrivare in stazione (intero maggiore di 0)
  - Tmax tempo in ms massimo prima che un nuovo treno arrivi in stazione (intero maggiore di Tmin)
  - (Facoltativo) Numero di treni che saranno simulati (intero maggiore di 0), di default è 100.

## Simulazioni 
Ho provato le combinazione di valori seguenti (che non portano a un aumento indeterminato dei treni in attesa):
  - N=3, T=1000, Tmin=10, Tmax=1200
  - N=8, T=2000, Tmin=100, Tmax=500
  - N=10, T=8000, Tmin=20, Tmax=3000
